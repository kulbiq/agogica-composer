#include "Placer.h"
#include "DspUtil.h"

namespace
{
// Window room beyond the HRIR length: the latest samples move back to the front once per
// this many samples (and runs of outputs may read up to 7 samples past the newest one).
constexpr int spareWindow = 2048;
static_assert (spareWindow >= History::maxBlock + 8);
} // namespace

void Placer::prepare (double sampleRate)
{
    hrtf = &Hrtf::forSampleRate (sampleRate);
    taps = hrtf->taps();
    convolve = dsp::bestConvolveTwo();

    window.assign ((size_t) (taps + spareWindow), 0.0f);
    clear();
    blended.assign ((size_t) taps * 2, 0.0f);

    pan.reset (sampleRate, 0.02);
    fadeStep = (float) (1.0 / (fadeSeconds * sampleRate));
    fadeLength = std::max (1, (int) std::lround (fadeSeconds * sampleRate));
    fadePosition = fadeLength;
    modeFade = 1.0f;
    lastPan = 2.0f;
}

void Placer::clear() noexcept
{
    std::fill (window.begin(), window.end(), 0.0f);
    end = taps - 1; // silence before the first sample
    filled = taps;
    live = true;
}

void Placer::setDirection (float azimuthDegrees, bool snap) noexcept
{
    // Callers subtract the head's yaw, so any angle can arrive; the stereo fold needs -180..180.
    azimuthDegrees = std::remainder (azimuthDegrees, 360.0f);
    const auto newPan = juce::jlimit (-1.0f, 1.0f, geometry::foldedAzimuth (azimuthDegrees) / 90.0f);

    // Hysteresis: keep the current HRIR until the angle is clearly nearer another one.
    if (const auto off = std::abs (std::remainder (azimuthDegrees - (float) targetDirection, 360.0f));
        snap || off > 0.5f + hysteresisDegrees)
        targetDirection = Hrtf::nearestIndex (azimuthDegrees);

    if (snap)
    {
        direction = nextDirection = targetDirection;
        fadePosition = fadeLength;
        pan.setCurrentAndTargetValue (newPan);
        lastPan = 2.0f;
        return;
    }

    pan.setTargetValue (newPan);
}

void Placer::setMode (OutputMode newMode, bool snap) noexcept
{
    if (newMode == mode)
        return;

    if (snap)
    {
        mode = newMode;
        modeFade = 1.0f;
        return;
    }

    // Switching back mid-fade starts from where the fade has got to.
    fadingFrom = mode;
    modeFade = modeFade < 1.0f ? 1.0f - modeFade : 0.0f;
    mode = newMode;
}

const float* Placer::ir (int index, int ear) const noexcept
{
    return hrtf->reversedIr (index, ear);
}

void Placer::pushWindow (const float* in, int n) noexcept
{
    if (end + n + 8 > (int) window.size())
    {
        std::copy (window.begin() + (end - (taps - 1)), window.begin() + end, window.begin());
        end = taps - 1;
    }

    std::copy (in, in + n, window.begin() + end);
    end += n;
    filled = std::min (taps, filled + n);
}

void Placer::binaural (int n, float* left, float* right) noexcept
{
    const auto* x = window.data() + (end - n - (taps - 1)); // the first output's window

    for (int done = 0; done < n;)
    {
        // Direction changes: fade to the latest direction once the previous fade is done.
        if (fadePosition >= fadeLength && targetDirection != direction)
        {
            nextDirection = targetDirection;
            fadePosition = 0;
        }

        if (fadePosition >= fadeLength)
        {
            convolve (ir (direction, 0), ir (direction, 1), taps, x + done, left + done, right + done, n - done);
            return;
        }

        const auto m = std::min (n - done, fadeLength - fadePosition);
        const auto apart = std::abs (nextDirection - direction);

        if (std::min (apart, Hrtf::numDirections - apart) <= blendDegrees)
        {
            // A small step: one blend of the two HRIRs for the rest of this block (or of the
            // fade), at the fade's position in the middle of it.
            const auto g = dsp::fadeGain (((float) fadePosition + 0.5f * (float) (m + 1)) / (float) fadeLength);

            for (int ear = 0; ear < 2; ++ear)
            {
                const auto* from = ir (direction, ear);
                const auto* to = ir (nextDirection, ear);
                auto* mixed = blended.data() + ear * taps;

                for (int j = 0; j < taps; ++j)
                    mixed[j] = from[j] + g * (to[j] - from[j]);
            }

            convolve (blended.data(), blended.data() + taps, taps, x + done, left + done, right + done, m);
        }
        else
        {
            // A jump: both convolutions, their outputs faded sample by sample.
            alignas (32) float toL[History::maxBlock], toR[History::maxBlock];
            convolve (ir (direction, 0), ir (direction, 1), taps, x + done, left + done, right + done, m);
            convolve (ir (nextDirection, 0), ir (nextDirection, 1), taps, x + done, toL, toR, m);

            for (int k = 0; k < m; ++k)
            {
                const auto g = dsp::fadeGain ((float) (fadePosition + k + 1) / (float) fadeLength);
                left[done + k] += g * (toL[k] - left[done + k]);
                right[done + k] += g * (toR[k] - right[done + k]);
            }
        }

        fadePosition += m;
        if (fadePosition >= fadeLength)
            direction = nextDirection;

        done += m;
    }
}

void Placer::process (const float* in, float* left, float* right, int n) noexcept
{
    jassert (n <= History::maxBlock);
    constexpr auto quarterPi = juce::MathConstants<float>::pi * 0.25f;

    // Constant-power pan law: -1..+1 -> 0..pi/2. Recomputed only when the pan moves, and only
    // while the stereo pan is heard (a turning head moves it all the time).
    float gainL[History::maxBlock] {}, gainR[History::maxBlock] {};
    const auto needStereo = mode == OutputMode::stereo || (modeFade < 1.0f && fadingFrom == OutputMode::stereo);

    if (! needStereo)
    {
        pan.skip (n);
        lastPan = 2.0f;
    }
    else for (int i = 0; i < n; ++i)
    {
        if (const auto p = pan.getNextValue(); ! juce::exactlyEqual (p, lastPan))
        {
            lastPan = p;
            panLeft = std::cos ((p + 1.0f) * quarterPi);
            panRight = std::sin ((p + 1.0f) * quarterPi);
        }

        gainL[i] = panLeft;
        gainR[i] = panRight;
    }

    const auto needBinaural = mode == OutputMode::binaural || (modeFade < 1.0f && fadingFrom == OutputMode::binaural);
    alignas (32) float binauralL[History::maxBlock] {}, binauralR[History::maxBlock] {};
    int waitFor = 0; // samples before a fade into binaural may start: the window must be full

    if (needBinaural)
    {
        // Back from a stretch in stereo (when the window isn't written): start from silence,
        // never from old input; the fade into binaural waits until the window is full again
        // (one HRIR length, ~5 ms), so it sounds as if the window had been written all along.
        if (! live)
        {
            std::fill (window.begin(), window.end(), 0.0f);
            end = taps - 1;
            filled = 0;
            live = true;
        }

        waitFor = std::max (0, taps - filled - 1);
        pushWindow (in, n);
        binaural (n, binauralL, binauralR);
    }
    else
    {
        // Stereo only: no HRIR work, and nothing to fade while binaural isn't heard.
        live = false;
        direction = nextDirection = targetDirection;
        fadePosition = fadeLength;
    }

    for (int i = 0; i < n; ++i)
    {
        const auto stereoL = in[i] * gainL[i], stereoR = in[i] * gainR[i];
        const auto stereo = mode == OutputMode::stereo;
        const auto newL = stereo ? stereoL : binauralL[i], newR = stereo ? stereoR : binauralR[i];

        if (! (modeFade < 1.0f))
        {
            left[i] += newL;
            right[i] += newR;
            continue;
        }

        if (i >= waitFor)
            modeFade = std::min (1.0f, modeFade + fadeStep);

        const auto g = dsp::fadeGain (modeFade);
        const auto oldL = stereo ? binauralL[i] : stereoL, oldR = stereo ? binauralR[i] : stereoR;
        left[i] += oldL + g * (newL - oldL);
        right[i] += oldR + g * (newR - oldR);
    }
}
