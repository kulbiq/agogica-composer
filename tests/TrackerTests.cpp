#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_core/juce_core.h>
#include "../tracker/engines/Decode.h"

using Catch::Matchers::WithinAbs;
using namespace engines;

TEST_CASE ("6DRepNet360: full heading from the Euler output")
{
    // The plan's convention cases: R = Rz(roll)·Ry(yaw)·Rx(pitch), heading = atan2(-R20, R00).
    CHECK_THAT (sixDRepNetHeading (30, 0, 0), WithinAbs (30, 1e-3));
    CHECK_THAT (sixDRepNetHeading (0, 180, 180), WithinAbs (180, 1e-3));
    CHECK_THAT (sixDRepNetHeading (-30, 180, 180), WithinAbs (-150, 1e-3));
    CHECK_THAT (sixDRepNetHeading (30, 180, 180), WithinAbs (150, 1e-3));
    CHECK_THAT (sixDRepNetHeading (-60, 5, -3), WithinAbs (-60, 0.1)); // small pitch/roll: nearly unchanged
}

TEST_CASE ("head continuity: the head nearest to the previous one")
{
    const std::vector<Detection> heads { { 1, { 0, 0, 10, 10 }, 0.9f }, { 1, { 100, 0, 110, 10 }, 0.5f } };

    CHECK (pickHead (heads, std::nullopt)->score > 0.8f);                    // first frame: the strongest
    CHECK (pickHead (heads, Box { 98, 0, 108, 10 })->box.x1 > 50.0f);         // then: the same person
    CHECK_FALSE (pickHead ({}, std::nullopt).has_value());
}

// ---- HeadFilter ------------------------------------------------------------------------------

#include "../tracker/engines/HeadFilter.h"
#include <random>

namespace
{
constexpr float frameTime = 1.0f / 30.0f;

// Feeds `frames` measurements at `yaw` (confidence c); returns the last state.
FilterState feed (HeadFilter& f, float yaw, int frames, float c = 1.0f)
{
    FilterState s;
    for (int i = 0; i < frames; ++i)
        s = f.update (Measurement { yaw, c }, frameTime);
    return s;
}
} // namespace

TEST_CASE ("head filter: still with noise - the output is much calmer than the input")
{
    HeadFilter f;
    std::mt19937 rng (1);
    std::normal_distribution<float> noise (0.0f, 4.0f);

    feed (f, 30.0f, 30);
    float rawJitter = 0, outJitter = 0, lastRaw = 30, lastOut = f.update (Measurement { 30, 1 }, frameTime).output;
    for (int i = 0; i < 300; ++i)
    {
        const auto raw = 30.0f + noise (rng);
        const auto out = f.update (Measurement { raw, 1 }, frameTime).output;
        rawJitter += std::abs (raw - lastRaw);
        outJitter += std::abs (out - lastOut);
        lastRaw = raw, lastOut = out;
    }
    CHECK (outJitter < 0.5f * rawJitter);
}

TEST_CASE ("head filter: outliers are ignored (a single wild frame, a mirrored burst)")
{
    SECTION ("a single wild frame")
    {
        HeadFilter f;
        const auto before = feed (f, 180.0f, 30).output;
        const auto s = feed (f, 10.0f, 1); // the user's example: 180 -> 10 for one frame
        CHECK (std::abs (wrap (s.output - before)) < 2.0f);
        CHECK (std::abs (wrap (feed (f, 180.0f, 10).output - 180.0f)) < 2.0f);
    }

    SECTION ("a burst of mirrored readings (180 - z) with a doubtful hemisphere")
    {
        HeadFilter f;
        feed (f, 110.0f, 30);
        const auto s = feed (f, mirrorOf (110.0f), 3, 0.3f);
        CHECK (std::abs (wrap (s.output - 110.0f)) < 3.0f);
    }
}

TEST_CASE ("head filter: re-acquisition after confirmFrames, approached at reacquireSpeed")
{
    SECTION ("a new direction wins after confirmFrames, not before")
    {
        FilterParams p;
        p.confirmFrames = 6;
        HeadFilter f (p);

        feed (f, 0.0f, 30);
        CHECK (std::abs (wrap (feed (f, 120.0f, p.confirmFrames / 2).estimate)) < 15.0f); // not yet

        HeadFilter g (p);
        feed (g, 0.0f, 30);
        CHECK (std::abs (wrap (feed (g, 120.0f, p.confirmFrames).estimate - 120.0f)) < 15.0f); // now
    }

    SECTION ("an accepted switch is approached slowly, never in a jump")
    {
        HeadFilter f;
        feed (f, 180.0f, 30);

        float last = f.update (Measurement { 180, 1 }, frameTime).output, worst = 0;
        bool arrived = false;
        for (int i = 0; i < 90; ++i) // 3 s at the new direction
        {
            const auto s = f.update (Measurement { 10.0f, 1 }, frameTime);
            worst = std::max (worst, std::abs (wrap (s.output - last)));
            last = s.output;
            arrived = arrived || std::abs (wrap (s.output - 10.0f)) < 2.0f;
        }
        CHECK (worst <= f.getParams().maxSpeed * frameTime + 1e-3f);       // never a jump
        CHECK (worst <= f.getParams().reacquireSpeed * frameTime + 1e-3f); // re-acquired at the slow speed
        CHECK (arrived);                                              // 170° at 90 °/s: under 2 s
    }
}

TEST_CASE ("head filter: genuine turns are followed (fast, across +-180)")
{
    SECTION ("a fast turn is followed without re-acquisition")
    {
        HeadFilter f;
        feed (f, 0.0f, 30);
        FilterState s;
        bool reacquired = false;
        for (int i = 1; i <= 12; ++i) // 90° in 0.4 s: 225 °/s
        {
            s = f.update (Measurement { 90.0f * (float) i / 12.0f, 1 }, frameTime);
            reacquired = reacquired || s.reacquiring;
        }
        s = feed (f, 90.0f, 6); // + 0.2 s
        CHECK_FALSE (reacquired);
        CHECK (std::abs (wrap (s.output - 90.0f)) < 5.0f);
    }

    SECTION ("a continuous turn across +-180 without a jump")
    {
        HeadFilter f;
        feed (f, 150.0f, 30);
        float last = f.update (Measurement { 150, 1 }, frameTime).output, worst = 0;
        for (int i = 1; i <= 60; ++i) // 150 -> 210 (= -150), 30 °/s
        {
            const auto s = f.update (Measurement { wrap (150.0f + (float) i), 1 }, frameTime);
            worst = std::max (worst, std::abs (wrap (s.output - last)));
            last = s.output;
        }
        CHECK (worst < 3.0f);
        CHECK (std::abs (wrap (last + 150.0f)) < 5.0f);
    }
}

TEST_CASE ("head filter: a lost head is held, then released")
{
    HeadFilter f;
    feed (f, 120.0f, 30);
    FilterState s;
    for (int i = 0; i < 60; ++i) // 2 s without a head, turned away: held
        s = f.update (std::nullopt, frameTime);
    CHECK (s.tracking);
    CHECK (std::abs (wrap (s.output - 120.0f)) < 5.0f);
    for (int i = 0; i < 40; ++i) // past 3 s: released
        s = f.update (std::nullopt, frameTime);
    CHECK_FALSE (s.tracking);

    HeadFilter g;
    feed (g, 5.0f, 30);
    for (int i = 0; i < 20; ++i) // facing the camera and gone for 0.67 s: the listener left
        s = g.update (std::nullopt, frameTime);
    CHECK_FALSE (s.tracking);
}

TEST_CASE ("hemisphere certainty: pitch/roll halfway means front/back is unknown")
{
    CHECK (hemisphereCertainty (3.8f, -13.9f) > 0.9f);     // facing the camera
    CHECK (hemisphereCertainty (-175.6f, 174.9f) > 0.9f);  // facing away
    CHECK (hemisphereCertainty (67.5f, 66.1f) < 0.2f);     // the recorded suspicious frames
    CHECK (hemisphereCertainty (38.8f, 44.3f) < 0.6f);
}

// Replays recorded sessions (tests/data/tracker-session-*.jsonl, 30 fps): the filter removes
// every jump of the raw model output and stays close to it. See the numbers: -s
TEST_CASE ("head filter: recorded sessions have no jumps and little lag", "[slow]")
{
    const auto files = juce::File (COMPOSER_TEST_DATA).findChildFiles (juce::File::findFiles, false, "tracker-session-*.jsonl");
    REQUIRE_FALSE (files.isEmpty());

    for (const auto& file : juce::File (COMPOSER_TEST_DATA).findChildFiles (juce::File::findFiles, false, "tracker-session-*.jsonl"))
    {
        HeadFilter f;
        juce::StringArray lines;
        file.readLines (lines);

        int rawJumps = 0, outJumps = 0, frames = 0;
        float lastRaw = 0, lastOut = 0, worstStep = 0, lagSum = 0;
        bool first = true;

        for (const auto& line : lines)
        {
            const auto v = juce::JSON::parse (line);
            std::optional<Measurement> m;
            if (v.hasProperty ("yaw"))
                m = Measurement { (float) v["yaw"], hemisphereCertainty ((float) v["pitch"], (float) v["roll"]) };

            const auto s = f.update (m, frameTime);
            if (! m || ! s.tracking)
                continue;

            if (! first)
            {
                rawJumps += std::abs (wrap (m->yaw - lastRaw)) > 30.0f;
                outJumps += std::abs (wrap (s.output - lastOut)) > 30.0f;
                worstStep = std::max (worstStep, std::abs (wrap (s.output - lastOut)));
            }
            lagSum += std::abs (wrap (s.output - m->yaw));
            lastRaw = m->yaw, lastOut = s.output, first = false, ++frames;
        }

        INFO (file.getFileName() << ": " << frames << " frames; steps > 30°: raw " << rawJumps << ", output " << outJumps
                                 << "; largest output step " << worstStep << "°; mean |output - raw| " << lagSum / (float) frames << "°");
        CHECK (frames > 1000);
        CHECK (rawJumps > 0); // the session does exercise the filter
        CHECK (outJumps == 0);
        CHECK (lagSum / (float) frames < 2.0f); // 1.2° when recorded
    }
}

// ---- FilterTuner -------------------------------------------------------------------------------

#include "../tracker/engines/FilterTuner.h"

namespace
{
using Pins = std::array<std::optional<float>, (size_t) Param::count>;

// Runs `seconds` of readings from `yawAt(t)` through a tuner; returns its settled parameters.
template <typename F>
FilterParams settle (FilterTuner& tuner, double& t, double seconds, F yawAt, const Pins& pins = {})
{
    FilterParams p;
    for (double end = t + seconds; t < end; t += 1.0 / 30.0)
    {
        tuner.add (t, yawAt (t), 1.0f);
        p = tuner.tuned (t, pins);
    }
    return p;
}
} // namespace

TEST_CASE ("filter tuner adapts: noise raises sigmaZ, motion raises turnSpeed, jumps count as outliers")
{
    SECTION ("noisy readings raise sigmaZ")
    {
        std::mt19937 rng (3);
        std::normal_distribution<float> small (0.0f, 1.0f), large (0.0f, 6.0f);
        FilterTuner calm, noisy;
        double t1 = 0, t2 = 0;
        const auto a = settle (calm, t1, 20, [&] (double) { return std::optional<float> (small (rng)); });
        const auto b = settle (noisy, t2, 20, [&] (double) { return std::optional<float> (large (rng)); });
        CHECK (b.sigmaZ > 2.5f * a.sigmaZ);
    }

    SECTION ("moving raises turnSpeed, never below the floor when still")
    {
        FilterTuner still, moving;
        double t1 = 0, t2 = 0;
        const auto a = settle (still, t1, 20, [] (double) { return std::optional<float> (10.0f); });
        const auto b = settle (moving, t2, 20, [] (double time) { return std::optional<float> (wrap ((float) (time * 200.0))); }); // 200 °/s
        CHECK (a.turnSpeed >= FilterTuner::minTurnSpeed);
        CHECK (b.turnSpeed > 250.0f);
    }

    SECTION ("model jumps count as outliers, not as speed")
    {
        FilterTuner tuner;
        double t = 0;
        int frame = 0;
        const auto p = settle (tuner, t, 20, [&] (double) { return std::optional<float> (++frame % 30 == 0 ? 170.0f : 10.0f); });
        CHECK (p.turnSpeed < 100.0f);    // the jumps didn't make it "fast"
        CHECK (p.outlierWeight > 0.05f); // they raised the outlier share
    }
}

TEST_CASE ("filter tuner: pinned values are respected")
{
    FilterTuner tuner;
    double t = 0;
    Pins pins;
    pins[(size_t) Param::sigmaZ] = 9.0f;
    pins[(size_t) Param::confirmFrames] = 10.0f;
    const auto p = settle (tuner, t, 10, [] (double time) { return std::optional<float> (wrap ((float) (time * 100.0))); }, pins);
    CHECK_THAT (p.sigmaZ, WithinAbs (9.0, 1e-6));
    CHECK (p.confirmFrames == 10);
}

#include "../tracker/engines/PoseSmoother.h"

TEST_CASE ("pose smoother: 8 fps steps become a smooth turn at 60 Hz, the short way round, no runaway at a stop")
{
    using engines::PoseSmoother;
    PoseSmoother s;
    constexpr double frame = 1.0 / 8, tick = 1.0 / 60;
    constexpr float speed = 90.0f; // degrees per second: from 150 through +-180 to -120
    auto input = [&] (double t) { return wrap (150.0f + speed * (float) std::min (t, 1.0)); };

    float last = 0, worstStep = 0, worstLag = 0, worstOvershoot = 0;
    double nextFrame = 0;
    for (double t = 0; t < 2.0; t += tick)
    {
        if (t >= nextFrame)
            s.push (input (nextFrame), nextFrame), nextFrame += frame;
        const auto y = s.step (t);
        if (t > 0.3)
        {
            worstStep = std::max (worstStep, std::abs (wrap (y - last)));
            if (t < 1.0)
                worstLag = std::max (worstLag, std::abs (wrap (input (t) - y)));
            else
                worstOvershoot = std::max (worstOvershoot, wrap (y - input (1.0)));
        }
        last = y;
    }
    CHECK (worstStep < speed * (float) tick * 2);        // no steps: at most twice the true motion per tick
    CHECK (worstLag < speed * (float) frame);            // behind by less than one camera frame
    CHECK (worstOvershoot < speed * (float) frame);      // a stop runs on by less than one frame
    CHECK_THAT (last, WithinAbs (input (2.0), 0.5));     // and settles where the head stopped

    s.reset();                                           // head lost, found elsewhere: start there
    s.push (-30.0f, 3.0);
    CHECK_THAT (s.step (3.0), WithinAbs (-30.0, 1e-3));
}

#include "../tracker/engines/StillGate.h"

TEST_CASE ("still gate: a still head reuses the estimate, a moving one or an old one doesn't")
{
    using engines::StillGate;
    // A "head": a bright disc with a dark eye at `eyeX`, plus camera noise.
    auto frame = [] (float eyeX, int noiseSeed)
    {
        juce::Image image (juce::Image::RGB, 160, 120, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colours::darkgrey);
        g.setColour (juce::Colours::bisque);
        g.fillEllipse (50, 30, 60, 60);
        g.setColour (juce::Colours::black);
        g.fillEllipse (eyeX, 50, 10, 10);
        juce::Random random (noiseSeed);
        for (int i = 0; i < 2000; ++i)
        {
            const auto x = random.nextInt (160), y = random.nextInt (120);
            image.setPixelAt (x, y, image.getPixelAt (x, y).brighter (0.05f));
        }
        return image;
    };
    const juce::Rectangle<float> head { 50 / 160.0f, 30 / 120.0f, 60 / 160.0f, 60 / 120.0f };

    StillGate gate;
    CHECK_FALSE (gate.unchanged (frame (70, 1), 0.0)); // nothing to compare with yet
    gate.reset (frame (70, 1), head, 0.0);

    CHECK (gate.unchanged (frame (70, 2), 0.1));       // only noise
    CHECK_FALSE (gate.unchanged (frame (85, 3), 0.2)); // the head turned
    CHECK_FALSE (gate.unchanged (frame (70, 4), StillGate::maxAgeSeconds)); // due for a full estimate

    gate.clear();                                      // head lost
    CHECK_FALSE (gate.unchanged (frame (70, 5), 0.3));
}
