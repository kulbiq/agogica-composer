#include "HeadTrackingFile.h"
#include "AgogicaFolder.h"

#include <cstring>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/file.h>
 #include <unistd.h>
#endif

namespace headtracking
{
namespace
{
// Changes when the file is replaced. On Windows JUCE's getFileIdentifier opens the file without
// FILE_SHARE_WRITE, so it fails (0) while the tracker holds it; this asks with full sharing.
juce::uint64 fileIdentity (const juce::File& file)
{
   #if JUCE_WINDOWS
    const auto h = CreateFileW (file.getFullPathName().toWideCharPointer(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    BY_HANDLE_FILE_INFORMATION info {};
    const auto ok = GetFileInformationByHandle (h, &info);
    CloseHandle (h);
    return ok ? ((juce::uint64) info.nFileIndexHigh << 32) | info.nFileIndexLow : 0;
   #else
    return file.getFileIdentifier();
   #endif
}
} // namespace

juce::File defaultFile()
{
    if (const auto overridden = juce::SystemStats::getEnvironmentVariable ("AGOGICA_HEAD_TRACKING_FILE", {}); overridden.isNotEmpty())
        return juce::File (overridden);

    return agogicaFolder().getChildFile ("head-tracking");
}

Writer::Writer (const juce::File& file)
{
    // Create it once at its final size (zero-filled temp file + atomic rename); from then
    // on it's only ever overwritten in place.
    if (file.getSize() != fileSize)
    {
        file.getParentDirectory().createDirectory();
        const auto temp = file.getSiblingFile (file.getFileName() + ".new");
        const juce::MemoryBlock blank (fileSize, true);

        if (! temp.replaceWithData (blank.getData(), blank.getSize()) || ! temp.moveFileTo (file))
            return;
    }

   #if ! JUCE_WINDOWS
    lockedFile = ::open (file.getFullPathName().toRawUTF8(), O_RDWR | O_CLOEXEC);

    if (lockedFile == -1 || ::flock (lockedFile, LOCK_EX | LOCK_NB) != 0)
        return; // another writer has it
   #endif

    map = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readWrite, false);

    if (map->getData() == nullptr || map->getSize() < (size_t) fileSize)
        return;

    record = static_cast<Record*> (map->getData());
    record->version.store (version, std::memory_order_relaxed);
    std::memcpy (record->magic, "AGHT", 4);
}

Writer::~Writer()
{
   #if ! JUCE_WINDOWS
    if (lockedFile != -1)
        ::close (lockedFile); // lets the lock go
   #endif
}

void Writer::write (const Pose& pose) noexcept
{
    if (record == nullptr)
        return;

    // Even before every write: a writer killed between its two stores leaves the count odd,
    // and without this every later "done" would be odd too - readers would never see one.
    const auto s = record->sequence.load (std::memory_order_relaxed) & ~1u;
    record->sequence.store (s + 1, std::memory_order_relaxed); // odd: writing
    std::atomic_thread_fence (std::memory_order_release);

    record->yaw.store (pose.yaw, std::memory_order_relaxed);
    record->pitch.store (pose.pitch, std::memory_order_relaxed);
    record->roll.store (pose.roll, std::memory_order_relaxed);
    record->timestampMs.store (pose.timestampMs, std::memory_order_relaxed);
    record->flags.store (pose.face ? faceFound : 0u, std::memory_order_relaxed);

    record->sequence.store (s + 2, std::memory_order_release); // even: done
}

Reader::Reader (juce::File f, juce::uint32 checkEvery) : file (std::move (f)), checkEveryMs (checkEvery) {}
Reader::~Reader() { close(); }

void Reader::close()
{
    if (handle == -1)
        return;

   #if JUCE_WINDOWS
    CloseHandle ((HANDLE) handle);
   #else
    ::close ((int) handle);
   #endif
    handle = -1;
}

void Reader::reopenIfNeeded (juce::uint32 now)
{
    if (checked && now - checkedAt < checkEveryMs)
        return;
    checked = true;
    checkedAt = now;

    const auto newIdentity = fileIdentity (file);
    const auto newSize = file.getSize();

    // Nothing new to look at - unless the last open failed: that one is tried again.
    if (handle != -1 && newIdentity == identity && newSize == size)
        return;

    close();
    identity = newIdentity;
    size = newSize;

    if (! file.existsAsFile() || size < fileSize)
        return;

    // Full sharing on Windows: the tracker holds the file open for writing, and may replace it.
   #if JUCE_WINDOWS
    const auto h = CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    handle = h == INVALID_HANDLE_VALUE ? -1 : (std::intptr_t) h;
   #else
    handle = ::open (file.getFullPathName().toRawUTF8(), O_RDONLY | O_CLOEXEC);
   #endif
}

bool Reader::copyOut (char* bytes) const
{
   #if JUCE_WINDOWS
    OVERLAPPED fromTheStart {};
    DWORD got = 0;
    return ReadFile ((HANDLE) handle, bytes, (DWORD) fileSize, &got, &fromTheStart) && got == (DWORD) fileSize;
   #else
    return ::pread ((int) handle, bytes, (size_t) fileSize, 0) == (ssize_t) fileSize;
   #endif
}

std::optional<Pose> Reader::read (juce::uint32 nowMs)
{
    reopenIfNeeded (nowMs);

    if (handle == -1)
        return lastRead = std::nullopt;

    const auto at = [] (const char* bytes, size_t offset, auto& value) { std::memcpy (&value, bytes + offset, sizeof (value)); };

    for (int attempt = 0; attempt < 8; ++attempt)
    {
        // Two copies. A copy isn't taken in one instant, so the writer may change the file
        // under it; but when both show the same even count, no write began between the
        // moments the two counts were copied - and all of the first copy was taken in between.
        char first[fileSize], second[fileSize];

        if (! copyOut (first) || ! copyOut (second))
            return lastRead = std::nullopt; // shorter than it was: emptied by something else

        // Checked on every read: a new file gets its magic just after it appears.
        std::uint32_t fileVersion = 0, before = 0, after = 0, flags = 0;
        at (first, offsetof (Record, version), fileVersion);

        if (std::memcmp (first, "AGHT", 4) != 0 || fileVersion != version)
            return lastRead = std::nullopt;

        at (first, offsetof (Record, sequence), before);
        at (second, offsetof (Record, sequence), after);

        if ((before & 1u) != 0 || before != after)
            continue; // a write is in progress, or one came in between

        Pose pose;
        at (first, offsetof (Record, yaw), pose.yaw);
        at (first, offsetof (Record, pitch), pose.pitch);
        at (first, offsetof (Record, roll), pose.roll);
        at (first, offsetof (Record, timestampMs), pose.timestampMs);
        at (first, offsetof (Record, flags), flags);
        pose.face = (flags & faceFound) != 0;

        // Angles that aren't numbers (a tracker gone wrong) are no head at all: a NaN would
        // go all the way to the audio and never ease away.
        if (! (std::isfinite (pose.yaw) && std::isfinite (pose.pitch) && std::isfinite (pose.roll)))
            pose = Pose { .timestampMs = pose.timestampMs };

        return lastRead = pose;
    }

    // The writer kept us busy: the pose we had still stands (it goes stale by its own
    // timestamp if this lasts).
    return lastRead;
}
} // namespace headtracking
