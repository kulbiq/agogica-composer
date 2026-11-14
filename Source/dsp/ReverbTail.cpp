#include "ReverbTail.h"

namespace
{
bool isPrime (int n)
{
    for (int d = 2; d * d <= n; ++d)
        if (n % d == 0)
            return false;
    return n > 1;
}

// In-place fast Walsh-Hadamard transform of 8 values, normalised (energy preserving).
void hadamard (std::array<float, ReverbTail::numLines>& v) noexcept
{
    for (size_t h = 1; h < v.size(); h <<= 1)
        for (size_t i = 0; i < v.size(); i += h << 1)
            for (size_t j = i; j < i + h; ++j)
            {
                const auto a = v[j], b = v[j + h];
                v[j] = a + b;
                v[j + h] = a - b;
            }

    for (auto& x : v)
        x *= 0.35355339f; // 1 / sqrt 8
}
} // namespace

void ReverbTail::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    for (size_t i = 0; i < lines.size(); ++i)
    {
        // Mutually prime lengths avoid coinciding echoes.
        auto length = (int) std::lround (lineMs[i] * sampleRate / 1000.0);
        while (! isPrime (length))
            ++length;

        lines[i].buffer.assign ((size_t) length, 0.0f);
        lines[i].pos = 0;
        lines[i].state = 0.0f;
    }

    input.prepare (sampleRate);
    level.reset (sampleRate, 0.05);
    hall = nullptr;
    snap = silent = true;
    dryFor = 0;
    hasEnergy = false;
    drySilentAfter = (int) (Acoustics::tailSeconds * sampleRate);
}

void ReverbTail::jumpToTargets() noexcept
{
    // After a silence longer than the tail: what's left in the lines is inaudible, but cleared
    // anyway so a render doesn't depend on what played before the silence.
    for (auto& line : lines)
    {
        std::fill (line.buffer.begin(), line.buffer.end(), 0.0f);
        line.state = 0.0f;
    }

    input.jumpToTarget();
    snap = true;
}

void ReverbTail::setAcoustics (const Acoustics& a) noexcept
{
    const auto& adjust = a.adjust;
    const auto directDelayMs = geometry::delayMs (a.player, a.listener);
    input.setTarget ((directDelayMs + a.hall->preDelayMs * adjust.preDelay) / 1000.0f * (float) sampleRate);

    const auto tailLevel = a.hall->tailLevel * adjust.reverb;

    if (snap)
        level.setCurrentAndTargetValue (tailLevel);
    else
        level.setTargetValue (tailLevel);

    snap = false;

    if ((a.hall != hall || ! juce::exactlyEqual (adjust.decay, decay)) && a.hall->rt60 > 0.0f)
    {
        // Per line: decay to -60 dB in rt60 at low frequencies, rt60High at high ones
        // (Jot's first-order absorptive filter: y = g (1 - p) x + p y).
        const auto rt60 = (double) (a.hall->rt60 * adjust.decay);
        const auto rt60High = (double) (a.hall->rt60High * adjust.decay);

        for (auto& line : lines)
        {
            const auto seconds = (double) line.buffer.size() / sampleRate;
            const auto gLow = std::pow (10.0, -3.0 * seconds / rt60);
            const auto gHigh = std::pow (10.0, -3.0 * seconds / rt60High);
            const auto ratio = gHigh / gLow;

            line.damping = (float) ((1.0 - ratio) / (1.0 + ratio));
            line.feedback = (float) gLow * (1.0f - line.damping);
        }
    }

    hall = a.hall;
    decay = adjust.decay;

    // Off (Dry, or the Pro reverb control at its bottom): keep running until what's left in
    // the lines has died away.
    if (tailLevel > 0.0f)
    {
        dryFor = 0;
        silent = false;
        hasEnergy = true;
    }
    else
    {
        silent = ! hasEnergy || dryFor >= drySilentAfter;
    }
}

void ReverbTail::processSample (const History& history, float& left, float& right) noexcept
{
    if (silent)
        return;

    if (level.getTargetValue() <= 0.0f)
        ++dryFor;

    std::array<float, numLines> out;

    for (size_t i = 0; i < lines.size(); ++i)
        out[i] = lines[i].buffer[(size_t) lines[i].pos];

    // Left and right: two different sign patterns over the lines -> decorrelated.
    const auto l = out[0] - out[1] + out[2] - out[3] + out[4] - out[5] + out[6] - out[7];
    const auto r = out[0] + out[1] - out[2] - out[3] + out[4] + out[5] - out[6] - out[7];

    hadamard (out);
    const auto in = input.read (history) * level.getNextValue();

    for (size_t i = 0; i < lines.size(); ++i)
    {
        auto& line = lines[i];
        line.state = line.feedback * out[i] + line.damping * line.state;
        line.buffer[(size_t) line.pos] = line.state + in;
        line.pos = line.pos + 1 < (int) line.buffer.size() ? line.pos + 1 : 0;
    }

    left += l * 0.35355339f;
    right += r * 0.35355339f;
}
