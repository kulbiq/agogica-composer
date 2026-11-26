#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>
#include <optional>

// Head orientation shared by the Agogica Tracker app with every Composer instance, through
// a 64-byte file - no network, no sockets.
//
// Protocol (version 1), see tracker/:
// - The writer creates the file once at its final size (temp file + atomic rename) and
//   afterwards only overwrites it in place (it maps it). It never truncates or recreates it
//   while running: plugins shipped before the reader stopped mapping the file would crash
//   their host when it shrinks. Today's reader copies the 64 bytes out with an ordinary read,
//   so a file emptied by anything else is just "no head".
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
    std::atomic<std::uint64_t> timestampMs; // juce::Time::currentTimeMillis() when the pose was written
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

// A pose written longer ago than this is stale: the tracker has stopped or stalled, and Composer
// eases back to straight ahead. The tracker treats a camera silent for this long the same way.
// Shipped plugins have this value built in: a tracker must write at least this often.
constexpr std::uint64_t staleAfterMs = 500;

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
// One writer at a time: two would spoil each other's count. On macOS a second one on the same
// file doesn't open (an advisory lock held as long as the writer lives); on Windows the app
// allows one instance of itself (ponytail: no lock there - a byte-range lock is mandatory on
// Windows and must not get in a reader's way; add one past the end of the file if a second
// kind of writer ever appears).
class Writer
{
public:
    explicit Writer (const juce::File&);
    ~Writer();
    bool isOpen() const noexcept { return record != nullptr; }
    void write (const Pose&) noexcept;

private:
    std::unique_ptr<juce::MemoryMappedFile> map;
    Record* record = nullptr;
    int lockedFile = -1;
};

// Reads the latest pose from the file (message thread). Tolerates a missing, short, emptied
// or foreign file and a file replaced under it (opens it again when its identity changes).
// The file itself is looked at (exists? replaced?) at most every `checkEveryMs`: reads come
// 100 times a second on the host's message thread, and each look is a few system calls; the
// reads in between are two small positional reads of the open file.
class Reader
{
public:
    explicit Reader (juce::File, juce::uint32 checkEveryMs = 500);
    ~Reader();

    // The latest consistent pose (the previous one while a write is in progress), or nothing
    // if there's no valid file.
    std::optional<Pose> read()   { return read (juce::Time::getMillisecondCounter()); }
    std::optional<Pose> read (juce::uint32 nowMs); // the clock handed in (tests)

private:
    void reopenIfNeeded (juce::uint32 nowMs);
    void close();
    bool copyOut (char* bytes) const; // the file's 64 bytes as they are now; false if it has fewer

    juce::File file;
    std::intptr_t handle = -1; // a file descriptor, or a Windows HANDLE
    std::optional<Pose> lastRead;
    juce::uint64 identity = 0;
    juce::int64 size = 0;
    juce::uint32 checkEveryMs, checkedAt = 0;
    bool checked = false;
};
} // namespace headtracking
