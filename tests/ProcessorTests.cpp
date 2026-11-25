// The whole processor as a host drives it: parameters, saved state, channel layouts,
// block sizes and real-time safety (no allocation on the audio thread).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "../Source/PluginProcessor.h"
#include "../Source/bridge/Stage.h"
#include "TrackingFileOverride.h"

#if JUCE_MAC
 #include <malloc/malloc.h>
#endif

using Catch::Matchers::WithinAbs;

// ---- Allocation counter --------------------------------------------------------------
// Counts heap allocations made by one thread (the "audio thread" of a test) while
// allocationsDuring() runs; other threads (JUCE, head tracking) allocate whenever they like.
// On macOS the malloc family itself is replaced for this binary, so juce::HeapBlock and
// AudioBuffer (which call std::malloc, not operator new) are seen too; elsewhere only new is.
// Sanitizer builds (asan/tsan presets) own malloc themselves: nothing is counted there.
#if defined (__has_feature)
 #if __has_feature (address_sanitizer) || __has_feature (thread_sanitizer)
  #define COMPOSER_SANITIZED 1
 #endif
#endif
#if defined (__SANITIZE_ADDRESS__) || defined (__SANITIZE_THREAD__)
 #define COMPOSER_SANITIZED 1
#endif

namespace
{
std::atomic<std::thread::id> countingThread {};
std::atomic<int> allocations { 0 };

void countAllocation() noexcept
{
    if (countingThread.load (std::memory_order_relaxed) == std::this_thread::get_id())
        allocations.fetch_add (1, std::memory_order_relaxed);
}

template <typename Function>
int allocationsDuring (Function&& f)
{
    allocations = 0;
    countingThread = std::this_thread::get_id();
    f();
    countingThread = std::thread::id {};
    return allocations;
}
} // namespace

#if ! COMPOSER_SANITIZED
#if JUCE_MAC
// Defined in the executable, so every call from this binary (tests, JUCE, the plugin code)
// binds here; system libraries keep their own. Memory comes from the default zone, which
// the system free() releases.
extern "C" void* malloc (size_t size)
{
    countAllocation();
    return malloc_zone_malloc (malloc_default_zone(), size);
}

extern "C" void* calloc (size_t count, size_t size)
{
    countAllocation();
    return malloc_zone_calloc (malloc_default_zone(), count, size);
}

extern "C" void* realloc (void* p, size_t size)
{
    countAllocation();
    auto* zone = p != nullptr ? malloc_zone_from_ptr (p) : nullptr;
    return malloc_zone_realloc (zone != nullptr ? zone : malloc_default_zone(), p, size);
}

extern "C" int posix_memalign (void** result, size_t alignment, size_t size)
{
    countAllocation();
    *result = malloc_zone_memalign (malloc_default_zone(), alignment, size);
    return *result != nullptr ? 0 : ENOMEM;
}
#endif

void* operator new (std::size_t size)
{
   #if ! JUCE_MAC
    countAllocation();
   #endif
    if (auto* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

// Windows has no posix_memalign, and its aligned blocks must go back through _aligned_free.
#if JUCE_WINDOWS
 #include <malloc.h>
static void* alignedAlloc (std::size_t alignment, std::size_t size) { return _aligned_malloc (size, alignment); }
static void alignedFree (void* p) { _aligned_free (p); }
#else
static void* alignedAlloc (std::size_t alignment, std::size_t size)
{
    void* p = nullptr;
    return posix_memalign (&p, alignment, size) == 0 ? p : nullptr;
}
static void alignedFree (void* p) { std::free (p); }
#endif

void* operator new (std::size_t size, std::align_val_t alignment)
{
   #if ! JUCE_MAC
    countAllocation();
   #endif
    if (auto* p = alignedAlloc (std::max ((std::size_t) alignment, sizeof (void*)), size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p) noexcept                                { std::free (p); }
void operator delete (void* p, std::size_t) noexcept                   { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept              { alignedFree (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { alignedFree (p); }
#endif // ! COMPOSER_SANITIZED

namespace
{
void set (ComposerProcessor& p, const char* id, float value)
{
    auto* param = p.state.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

size_t peakIndex (const std::vector<float>& v)
{
    return (size_t) std::distance (v.begin(), std::max_element (v.begin(), v.end(), [] (float a, float b)
    {
        return std::abs (a) < std::abs (b);
    }));
}

void fillNoise (juce::AudioBuffer<float>& buffer, juce::Random& rng)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i, rng.nextFloat() - 0.5f);
}
} // namespace

TEST_CASE ("roomSize defaults to 20 m and snaps to 5 m steps", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    CHECK_THAT (p.getParam ("roomSize"), WithinAbs (20.0, 1e-4));

    set (p, "roomSize", 12.0f);
    CHECK_THAT (p.getParam ("roomSize"), WithinAbs (10.0, 1e-4));

    set (p, "roomSize", 13.0f);
    CHECK_THAT (p.getParam ("roomSize"), WithinAbs (15.0, 1e-4));

    set (p, "roomSize", 80.0f);
    CHECK_THAT (p.getParam ("roomSize"), WithinAbs (50.0, 1e-4));
}

TEST_CASE ("input level is shared by all readers and falls without audio", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    p.prepareToPlay (48000.0, 64);

    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();
    buffer.setSample (0, 10, 0.5f);
    juce::MidiBuffer midi;
    p.processBlock (buffer, midi);

    CHECK_THAT (p.readInputLevel (1000), WithinAbs (0.5, 1e-6));
    CHECK_THAT (p.readInputLevel (1000), WithinAbs (0.5, 1e-6)); // a second editor, same tick
    CHECK (p.readInputLevel (1300) < 0.5f * 0.4f);
    CHECK (p.readInputLevel (3000) < 0.001f);
}

TEST_CASE ("DSP load: average, worst block and overruns per period; offline renders don't count", "[integration]")
{
    SECTION ("the meter")
    {
        LoadMeter meter;
        meter.add (0.001, 0.010); // 10 % of its block
        meter.add (0.020, 0.010); // twice its block: an overrun
        const auto r = meter.read();
        CHECK_THAT (r.average, WithinAbs (1.05, 1e-6)); // 21 ms of work in 20 ms of audio
        CHECK_THAT (r.peak, WithinAbs (2.0, 1e-6));
        CHECK (r.overruns == 1);

        const auto next = meter.read(); // each read starts a new period
        CHECK_THAT (next.average, WithinAbs (0.0, 0.0)); // exactly: nothing was measured
        CHECK_THAT (next.peak, WithinAbs (0.0, 0.0));
        CHECK (next.overruns == 0);
    }

    SECTION ("in processBlock")
    {
        juce::ScopedJuceInitialiser_GUI juce;
        ComposerProcessor p;
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;

        buffer.clear();
        p.processBlock (buffer, midi);
        CHECK (p.load.read().average > 0.0f);

        p.setNonRealtime (true);
        p.processBlock (buffer, midi);
        CHECK_THAT (p.load.read().average, WithinAbs (0.0, 0.0)); // exactly: exports aren't measured
    }
}

TEST_CASE ("processBlock: position from parameters, clamped to the room", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    set (p, "roomSize", 10.0f);
    set (p, "playerX", 20.0f); // clamped to the edge: x = 5
    set (p, "playerY", 5.0f);

    constexpr double sampleRate = 48000.0;
    p.setPlayConfigDetails (2, 2, sampleRate, 512);
    p.prepareToPlay (sampleRate, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    std::vector<float> left;

    for (int block = 0; block < 4; ++block)
    {
        buffer.clear();
        if (block == 0)
            buffer.setSample (0, 0, 1.0f);

        p.processBlock (buffer, midi);
        left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 512);
    }

    const auto expected = std::hypot (5.0, 5.0) / 343.0 * sampleRate; // ~989.6 samples
    CHECK (std::abs ((double) peakIndex (left) - expected) <= 1.0);
}

TEST_CASE ("processBlock never allocates (both modes, Pro, moves and mode switches)", "[integration]")
{
    // Control: the counter sees every way the code under test could allocate.
    // Elsewhere only operator new is counted; the macOS run covers the malloc family.
   #if COMPOSER_SANITIZED
    WARN ("sanitizer build: allocations aren't counted (the regular build checks them)");
   #else
    static void* volatile sink = nullptr;
   #endif
   #if JUCE_MAC && ! COMPOSER_SANITIZED
    CHECK (allocationsDuring ([] { sink = std::malloc (16); std::free (sink); }) == 1);
    CHECK (allocationsDuring ([] { sink = std::calloc (4, 4); std::free (sink); }) == 1);
    CHECK (allocationsDuring ([] { sink = std::malloc (16); sink = std::realloc (sink, 4096); std::free (sink); }) == 2);
    CHECK (allocationsDuring ([] { juce::HeapBlock<float> block (64); sink = block.get(); }) == 1);
   #endif
   #if ! COMPOSER_SANITIZED
    CHECK (allocationsDuring ([] { auto* f = new float[64]; sink = f; delete[] f; }) == 1);
   #endif

    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (3);
    int total = 0;

    // Parameters change between blocks, as a host's message thread would change them.
    auto play = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            fillNoise (buffer, rng);
            total += allocationsDuring ([&] { p.processBlock (buffer, midi); });
        }
    };

    for (const auto mode : { 1.0f, 0.0f })
    {
        set (p, "outputMode", mode);
        for (const auto x : { -8.0f, 3.0f, 9.0f })
        {
            set (p, "playerX", x); // a player dragged mid-stream: direction and delay fades
            play (2);
        }
    }

    set (p, "proDistanceLaw", 6.0f);
    play (2);

    set (p, "outputMode", 1.0f); // a mode switch mid-stream
    play (2);

    CHECK (total == 0);
}

TEST_CASE ("silence skip: never cuts the longest tail, resumes at the current position", "[slow][integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    juce::MidiBuffer midi;

    SECTION ("the longest delay (opposite corners of the largest room) is never cut")
    {
        // The direct sound alone: the output ends with the longest delay (70.7 m, 0.21 s), well
        // inside the 0.5 s the track keeps processing after its input stops.
        constexpr double sampleRate = 48000.0;
        set (p, "roomSize", 50.0f);
        set (p, "playerX", 25.0f);
        set (p, "playerY", 25.0f);
        set (p, "listenerX", -25.0f);
        set (p, "listenerY", -25.0f);
        p.setPlayConfigDetails (2, 2, sampleRate, 480);
        p.prepareToPlay (sampleRate, 480);

        juce::AudioBuffer<float> buffer (2, 480);
        std::vector<float> left;
        for (int b = 0; b < 100; ++b) // an impulse, then 1 s of silence
        {
            buffer.clear();
            if (b == 0)
                buffer.setSample (0, 0, 1.0f), buffer.setSample (1, 0, 1.0f);
            p.processBlock (buffer, midi);
            left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 480);
        }

        const auto arrival = std::hypot (50.0, 50.0) / 343.0 * sampleRate;
        REQUIRE (arrival < ComposerProcessor::tailSeconds * sampleRate);
        CHECK (std::abs ((double) peakIndex (left) - arrival) <= 1.0);
        CHECK (std::abs (left[peakIndex (left)]) > 0.01f);
    }

    SECTION ("a silent track skips its work and resumes at the current position, without allocating")
    {
        constexpr double sampleRate = 48000.0;
        p.setPlayConfigDetails (2, 2, sampleRate, 480);
        p.prepareToPlay (sampleRate, 480);

        juce::AudioBuffer<float> buffer (2, 480);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        p.processBlock (buffer, midi);

        for (int block = 0; block < 100; ++block) // 1 s of silence (longer than the 0.5 s tail)
        {
            buffer.clear();
            p.processBlock (buffer, midi);
        }

        CHECK (buffer.getMagnitude (0, 480) <= 0.0f);

        set (p, "playerY", 10.0f); // moved while silent: 10 m -> 1399.4 samples
        std::vector<float> left;
        int allocated = 0;
        for (int block = 0; block < 4; ++block)
        {
            buffer.clear();
            if (block == 0)
                buffer.setSample (0, 0, 1.0f);
            allocated += allocationsDuring ([&] { p.processBlock (buffer, midi); });
            left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 480);
        }

        CHECK (allocated == 0);
        CHECK (std::abs ((double) peakIndex (left) - 10.0 / 343.0 * sampleRate) <= 1.0);
    }

    SECTION ("after a silence a track starts clean, never with old input")
    {
        constexpr double sampleRate = 48000.0;
        const auto mode = GENERATE (0.0f, 1.0f);
        set (p, "outputMode", mode);
        p.setPlayConfigDetails (2, 2, sampleRate, 480);
        p.prepareToPlay (sampleRate, 480);

        juce::AudioBuffer<float> buffer (2, 480);
        juce::Random rng (5);
        auto run = [&] (int blocks, bool noise)
        {
            for (int b = 0; b < blocks; ++b)
            {
                if (noise)
                    fillNoise (buffer, rng);
                else
                    buffer.clear();
                p.processBlock (buffer, midi);
            }
        };

        run (100, true);   // 1 s of noise
        run (100, false);  // 1 s of silence: the track skips its work

        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        buffer.setSample (1, 0, 1.0f);
        p.processBlock (buffer, midi);

        // Nothing can arrive before the direct sound (5 m: ~700 samples later).
        CHECK (buffer.getMagnitude (0, 0, 480) <= 0.0f);
        CHECK (buffer.getMagnitude (1, 0, 480) <= 0.0f);
    }
}

TEST_CASE ("block size doesn't change the result (1-sample blocks included)", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto mode = GENERATE (0.0f, 1.0f); // stereo, binaural
    constexpr int length = 9600;

    auto render = [&] (int blockSize)
    {
        ComposerProcessor p;
        set (p, "outputMode", mode);
        set (p, "playerX", -4.0f);
        p.setPlayConfigDetails (2, 2, 48000.0, blockSize);
        p.prepareToPlay (48000.0, blockSize);

        juce::Random rng (5);
        std::vector<float> left, right;
        juce::MidiBuffer midi;

        for (int done = 0; done < length; done += blockSize)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (0, i, rng.nextFloat() * 2.0f - 1.0f), buffer.setSample (1, i, 0.0f);

            p.processBlock (buffer, midi);
            left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            right.insert (right.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + blockSize);
        }

        return std::pair { left, right };
    };

    const auto big = render (480);
    const auto blockSize = GENERATE (1, 37);
    const auto other = render (blockSize);

    for (size_t i = 0; i < (size_t) length; ++i)
    {
        REQUIRE_THAT (other.first[i], WithinAbs (big.first[i], 1e-6));
        REQUIRE_THAT (other.second[i], WithinAbs (big.second[i], 1e-6));
    }
}

TEST_CASE ("channels: stereo input is summed to mono, mono input is used as is", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;

    // Renders 4 blocks; `fill` writes each input sample from two noise values.
    auto render = [] (int inputs, auto fill)
    {
        ComposerProcessor p;
        set (p, "playerX", -3.0f); // off-centre: left and right differ
        p.setPlayConfigDetails (inputs, 2, 48000.0, 256);
        REQUIRE (p.getTotalNumInputChannels() == inputs);
        p.prepareToPlay (48000.0, 256);

        juce::Random rng (6);
        juce::MidiBuffer midi;
        std::vector<float> out;
        for (int b = 0; b < 4; ++b)
        {
            juce::AudioBuffer<float> buffer (2, 256);
            for (int i = 0; i < 256; ++i)
            {
                const auto x = rng.nextFloat() - 0.5f;
                fill (buffer, i, x, rng.nextFloat() - 0.5f);
            }
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.insert (out.end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + 256);
        }
        return out;
    };

    const auto stereo = render (2, [] (juce::AudioBuffer<float>& buffer, int i, float a, float b)
    {
        buffer.setSample (0, i, a), buffer.setSample (1, i, b);
    });
    const auto summed = render (2, [] (juce::AudioBuffer<float>& buffer, int i, float a, float b)
    {
        buffer.setSample (0, i, 0.5f * (a + b)), buffer.setSample (1, i, 0.5f * (a + b));
    });
    // Mono: channel 1 is output-only (hosts may leave anything in it): it must be ignored.
    const auto mono = render (1, [] (juce::AudioBuffer<float>& buffer, int i, float a, float b)
    {
        buffer.setSample (0, i, 0.5f * (a + b)), buffer.setSample (1, i, 0.9f);
    });

    CHECK (stereo == summed);
    CHECK (mono == summed);

    SECTION ("before prepareToPlay, or with no input bus, a block is silence - never a crash")
    {
        juce::MidiBuffer midi;
        ComposerProcessor unprepared;
        juce::AudioBuffer<float> buffer (2, 64);
        buffer.setSample (0, 3, 0.7f);
        unprepared.processBlock (buffer, midi);
        CHECK (buffer.getMagnitude (0, 64) <= 0.0f);

        ComposerProcessor noInput;
        noInput.setPlayConfigDetails (0, 2, 48000.0, 64);
        noInput.prepareToPlay (48000.0, 64);
        buffer.setSample (1, 5, 0.7f);
        noInput.processBlock (buffer, midi);
        CHECK (buffer.getMagnitude (0, 64) <= 0.0f);
    }
}

TEST_CASE ("a project saved in Binaural starts in Binaural: no fade from the stereo pan", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;

    // An impulse after `silentBlocks` of silence (a mode fade would be over after 20 ms of them).
    auto impulseResponse = [] (int silentBlocks)
    {
        ComposerProcessor p;
        set (p, "outputMode", 1.0f);
        set (p, "playerX", 0.0f);
        set (p, "playerY", 0.5f); // 0.5 m: the sound arrives within the first block
        p.prepareToPlay (48000.0, 256);

        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer (2, 256);
        for (int block = 0; block < silentBlocks; ++block)
        {
            buffer.clear();
            p.processBlock (buffer, midi);
        }

        std::vector<float> out;
        for (int block = 0; block < 8; ++block)
        {
            buffer.clear();
            if (block == 0)
                buffer.setSample (0, 0, 1.0f), buffer.setSample (1, 0, 1.0f);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.insert (out.end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + 256);
        }
        return out;
    };

    const auto atOnce = impulseResponse (0), later = impulseResponse (20);
    for (size_t i = 0; i < atOnce.size(); ++i)
        REQUIRE_THAT (atOnce[i], WithinAbs (later[i], 1e-6));
}

TEST_CASE ("bypass: the dry sound on both channels, and nothing old when it ends", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    const auto mode = GENERATE (0.0f, 1.0f);
    ComposerProcessor p;
    set (p, "outputMode", mode);
    p.setPlayConfigDetails (1, 2, 48000.0, 256); // a mono track
    p.prepareToPlay (48000.0, 256);

    juce::Random rng (11);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (2, 256);

    for (int block = 0; block < 100; ++block) // sound on its way through the delay
    {
        fillNoise (buffer, rng);
        p.processBlock (buffer, midi);
    }

    fillNoise (buffer, rng);
    const juce::AudioBuffer<float> dry (buffer);
    p.processBlockBypassed (buffer, midi);
    for (int i = 0; i < 256; ++i)
    {
        REQUIRE (buffer.getSample (0, i) == dry.getSample (0, i));
        REQUIRE (buffer.getSample (1, i) == dry.getSample (0, i)); // not left-only
    }

    // Back on, with a silent input: what was in the delay before the bypass is not heard now.
    for (int block = 0; block < 60; ++block)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        REQUIRE (buffer.getMagnitude (0, 256) == 0.0f);
    }
}

TEST_CASE ("bus layouts: mono or stereo in, stereo out only", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    using Set = juce::AudioChannelSet;
    const auto [in, out, supported] = GENERATE (table<Set, Set, bool> ({
        { Set::mono(), Set::stereo(), true },
        { Set::stereo(), Set::stereo(), true },
        { Set::disabled(), Set::stereo(), false },
        { Set::createLCR(), Set::stereo(), false },
        { Set::stereo(), Set::mono(), false },
        { Set::stereo(), Set::create5point1(), false },
    }));

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (in);
    layout.outputBuses.add (out);
    CAPTURE (in.getDescription(), out.getDescription());
    CHECK (p.isBusesLayoutSupported (layout) == supported);
}

// ---- Saved state -------------------------------------------------------------------------

namespace
{
// A project saved by an earlier build (values below). Never regenerate it: every later
// build must keep loading it.
juce::MemoryBlock savedProject()
{
    juce::MemoryBlock data;
    REQUIRE (juce::File (juce::String (COMPOSER_TEST_DATA) + "/state-v1.bin").loadFileAsData (data));
    return data;
}

void load (ComposerProcessor& p, const juce::MemoryBlock& data)
{
    p.setStateInformation (data.getData(), (int) data.getSize());
}
} // namespace

TEST_CASE ("state: a saved project restores, unknown parameters keep defaults, junk is ignored", "[integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    SECTION ("a project saved by an earlier build restores")
    {
        load (p, savedProject());

        CHECK_THAT (p.getParam ("roomSize"), WithinAbs (35.0, 1e-4));
        CHECK_THAT (p.getParam ("listenerX"), WithinAbs (1.0, 1e-4));
        CHECK_THAT (p.getParam ("listenerY"), WithinAbs (-2.0, 1e-4));
        CHECK_THAT (p.getParam ("playerX"), WithinAbs (6.0, 1e-4));
        CHECK_THAT (p.getParam ("playerY"), WithinAbs (4.0, 1e-4));
        CHECK_THAT (p.getParam ("outputMode"), WithinAbs (1.0, 1e-4));
        CHECK (p.getDisplayName() == "Viola");
        CHECK (p.getScene() == "D");
        CHECK (p.isPlaced());

        stage::setScene (p, "A");
        CHECK_THAT (p.getParam ("playerX"), WithinAbs (-3.0, 1e-4));
    }

    SECTION ("a parameter the project doesn't know yet keeps its default")
    {
        const auto data = savedProject();
        auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), (int) data.getSize());
        REQUIRE (xml != nullptr);
        auto* mode = xml->getChildByAttribute ("id", "outputMode");
        REQUIRE (mode != nullptr);
        xml->removeChildElement (mode, true);

        juce::MemoryBlock older;
        juce::AudioProcessor::copyXmlToBinary (*xml, older);
        load (p, older);

        CHECK_THAT (p.getParam ("outputMode"), WithinAbs (0.0, 1e-4));
        CHECK_THAT (p.getParam ("roomSize"), WithinAbs (35.0, 1e-4));
    }

    SECTION ("empty, garbage, truncated or foreign data leaves the state untouched")
    {
        set (p, "roomSize", 15.0f);
        p.setNameOverride ("Cello");

        const auto data = savedProject();
        juce::MemoryBlock garbage (256);
        juce::Random (9).fillBitsRandomly (garbage.getData(), garbage.getSize());
        juce::MemoryBlock foreign;
        juce::AudioProcessor::copyXmlToBinary (juce::XmlElement ("OtherPlugin"), foreign);

        p.setStateInformation (nullptr, 0);
        load (p, garbage);
        p.setStateInformation (data.getData(), (int) data.getSize() / 2);
        load (p, foreign);

        CHECK_THAT (p.getParam ("roomSize"), WithinAbs (15.0, 1e-4));
        CHECK (p.getDisplayName() == "Cello");
    }
}

TEST_CASE ("parameters: IDs, ranges, defaults and choices never change silently", "[integration]")
{
    // Hosts save automation and projects by parameter ID, version and normalised value,
    // and choices by index: changing any of these breaks existing projects. If a change is
    // intended, update tests/data/parameters.txt (the expected text is printed on failure).
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;

    auto number = [] (double v) { return juce::String (std::abs (v) < 5e-5 ? 0.0 : v, 4); }; // no "-0.0000"

    juce::StringArray lines;
    for (auto* param : p.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        REQUIRE (ranged != nullptr);
        const auto& range = ranged->getNormalisableRange();
        auto line = ranged->getParameterID() + " v" + juce::String (ranged->getVersionHint())
                  + " \"" + ranged->getName (100) + "\" " + number (range.start) + ".." + number (range.end)
                  + " step " + number (range.interval) + " skew " + number (range.skew)
                  + " default " + number (ranged->convertFrom0to1 (ranged->getDefaultValue()));
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
            line << " choices " << choice->choices.joinIntoString ("|");
        lines.add (line);
    }

    const auto actual = lines.joinIntoString ("\n") + "\n";
    const auto expected = juce::File (juce::String (COMPOSER_TEST_DATA) + "/parameters.txt").loadFileAsString();
    INFO (actual);
    CHECK (actual == expected);
}

TEST_CASE ("output stays finite for any parameter values (fuzz)", "[slow][integration]")
{
    juce::ScopedJuceInitialiser_GUI juce;
    ComposerProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);

    juce::Random rng (1234);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    juce::StringArray ids;
    for (auto* param : p.getParameters()) // every parameter, Pro included
        ids.add (dynamic_cast<juce::AudioProcessorParameterWithID&> (*param).paramID);
    REQUIRE (ids.size() >= 7);

    for (int block = 0; block < 500; ++block)
    {
        // Mostly random values, sometimes the extremes (players and listener meeting on a wall).
        for (const auto& id : ids)
            p.state.getParameter (id)->setValueNotifyingHost (rng.nextInt (4) == 0 ? (float) rng.nextInt (2) : rng.nextFloat());

        for (int i = 0; i < 256; ++i)
            buffer.setSample (0, i, rng.nextFloat() * 2.0f - 1.0f), buffer.setSample (1, i, rng.nextFloat() * 2.0f - 1.0f);

        p.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                REQUIRE (std::isfinite (buffer.getSample (ch, i)));
    }
}

// Release only: build/release/ComposerTests "[perf]" -s
TEST_CASE ("CPU: 32 players", "[.perf]")
{
   #if JUCE_DEBUG
    SKIP ("CPU limits apply to Release builds"); // (the body below is Release only: no unreachable code)
   #else
    const auto sampleRate = GENERATE (48000.0, 96000.0);
    // A turning head (binaural, live) re-places every source all the time: a direction fade
    // runs on every player.
    const auto [mode, headTurning] = GENERATE (table<float, bool> ({ { 0.0f, false }, { 1.0f, false }, { 1.0f, true } }));
    constexpr int players = 32, block = 512;
    constexpr double seconds = 10.0;

    const auto trackingFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("composer-perf-head-tracking");
    trackingFile.deleteFile();
    const TrackingFileOverride trackingOverride (trackingFile);
    headtracking::Writer head (trackingFile);
    juce::ScopedJuceInitialiser_GUI juce;

    std::vector<std::unique_ptr<ComposerProcessor>> instances;
    for (int i = 0; i < players; ++i)
    {
        auto& p = *instances.emplace_back (std::make_unique<ComposerProcessor>());
        auto set = [&] (const char* id, float v)
        {
            auto* param = p.state.getParameter (id);
            param->setValueNotifyingHost (param->convertTo0to1 (v));
        };
        set ("outputMode", mode);
        set ("playerX", (float) (i % 8) * 2.0f - 7.0f);
        set ("playerY", (float) (i / 8) * 2.0f + 3.0f);
        p.setPlayConfigDetails (2, 2, sampleRate, block);
        p.prepareToPlay (sampleRate, block);
    }

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    juce::Random rng (9);
    const auto blocks = (int) (seconds * sampleRate / block);

    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
    {
        if (headTurning) // 90 degrees a second, back and forth, polled like the 100 Hz timer
        {
            const auto t = b * block / sampleRate;
            head.write ({ (float) (90.0 * std::sin (t)), 0.0f, 0.0f, (std::uint64_t) juce::Time::currentTimeMillis(), true });
            instances.front()->headTracking->pollNow();
        }

        for (auto& p : instances)
        {
            for (int i = 0; i < block; ++i)
                buffer.setSample (0, i, rng.nextFloat() - 0.5f), buffer.setSample (1, i, 0.0f);
            p->processBlock (buffer, midi);
        }
    }
    const auto load = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0 / seconds;

    UNSCOPED_INFO (players << " players, " << (mode > 0.5f ? "binaural" : "stereo") << (headTurning ? ", head turning" : "")
                           << ", " << sampleRate << " Hz: " << juce::String (load * 100.0, 1) << " % of one core");

    // Regression guards per platform (multi-core hosts spread tracks over cores).
    const auto rate48k = sampleRate < 50000.0;
    const auto stereo = mode < 0.5f;
   #if JUCE_WINDOWS
    // i5-10210U laptop on mains power (2026-11-15, median of 3 runs), the direct sound alone in
    // blocks: stereo / binaural / binaural with a turning head 2.2 / 3.8 / 5.3 % at 48 kHz,
    // 4.4 / 10.5 / 16.3 % at 96 kHz; ~40 % margin (small numbers vary more).
    CHECK (load < (rate48k ? (stereo ? 0.031 : headTurning ? 0.075 : 0.054)
                           : (stereo ? 0.062 : headTurning ? 0.23 : 0.15)));
   #else
    // ponytail: still the whole chain's limits (M4 Pro); measure the direct sound alone there.
    juce::ignoreUnused (stereo);
    CHECK (load < (rate48k ? 0.30 : 0.90) * (headTurning ? 1.4 : 1.0));
   #endif
   #endif
}
