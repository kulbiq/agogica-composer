#pragma once

#include <juce_events/juce_events.h>
#include "StageRegistry.h"
#include <atomic>
#include <cstdint>

// Head orientation shared by the Agogica Tracker app with every Composer instance, through
// a 64-byte memory-mapped file - no network, no sockets.
//
// Protocol (version 1), see tracker/:
// - The writer creates the file once at its final size (temp file + atomic rename) and
//   afterwards only overwrites it in place. It never truncates or recreates it while
//   running: a mapped file shrinking under a reader is a crash inside the host.
// - Seqlock: `sequence` is odd while a write is in progress.
// - yaw is in degrees, clockwise = head turned right (towards +x on the map).
namespace headtracking
{
struct Record
{
    char magic[4];                        // "AGHT"
    std::atomic<std::uint32_t> version;   // 1
    std::atomic<std::uint32_t> sequence;
    std::atomic<std::uint32_t> flags;     // bit 0: a face is being tracked
    std::atomic<float> yaw, pitch, roll;  // degrees
    std::atomic<std::uint64_t> timestampMs; // juce::Time::currentTimeMillis() of the frame
};

static_assert (std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free
               && std::atomic<std::uint64_t>::is_always_lock_free, "the record is shared between processes");

constexpr int fileSize = 64;
static_assert (sizeof (Record) <= fileSize);

// The byte layout is the protocol (tools/fake_tracker.py and the tracker app write it).
static_assert (offsetof (Record, version) == 4 && offsetof (Record, sequence) == 8 && offsetof (Record, flags) == 12
               && offsetof (Record, yaw) == 16 && offsetof (Record, pitch) == 20 && offsetof (Record, roll) == 24
               && offsetof (Record, timestampMs) == 32, "head-tracking file layout changed");
constexpr std::uint32_t version = 1;
constexpr std::uint32_t faceFound = 1;

// ~/Library/Application Support/Agogica/head-tracking, %LOCALAPPDATA%\Agogica\head-tracking.
// The AGOGICA_HEAD_TRACKING_FILE environment variable overrides it (tests, development).
juce::File defaultFile();

struct Pose
{
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    std::uint64_t timestampMs = 0;
    bool face = false;
};

// Writes poses into the file (the tracker app; tests; tools/fake_tracker.py does the same).
class Writer
{
public:
    explicit Writer (const juce::File&);
    bool isOpen() const noexcept { return record != nullptr; }
    void write (const Pose&) noexcept;

private:
    std::unique_ptr<juce::MemoryMappedFile> map;
    Record* record = nullptr;
};

// Reads the latest pose from the file (message thread). Tolerates a missing, short or
// foreign file and a file replaced under it (re-maps when its identity changes).
class Reader
{
public:
    explicit Reader (juce::File);

    // The latest consistent pose, or nothing if there's no valid file.
    std::optional<Pose> read();

private:
    void remapIfNeeded();

    juce::File file;
    std::unique_ptr<juce::MemoryMappedFile> map;
    const Record* record = nullptr;
    juce::uint64 identity = 0;
    juce::int64 size = 0;
};
} // namespace headtracking

// Per process: polls the file every 10 ms while some Composer instance is in Binaural
// (checked on the registry every tick; otherwise it idles at 2 Hz),
// and gives the audio threads a smoothed yaw. Data older than 0.5 s or no face: not live,
// and the yaw eases back to straight ahead.
class HeadTracking : private juce::Timer
{
public:
    HeadTracking();
    ~HeadTracking() override;

    float yaw() const noexcept   { return yawDegrees.load (std::memory_order_relaxed); }
    bool isLive() const noexcept { return live.load (std::memory_order_relaxed); }

    // One poll (the timer calls this; tests too, having no message loop).
    void pollNow();

private:
    void timerCallback() override;

    juce::SharedResourcePointer<StageRegistry> registry;
    headtracking::Reader reader { headtracking::defaultFile() };
    std::atomic<float> yawDegrees { 0.0f };
    std::atomic<bool> live { false };
};
