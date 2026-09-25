#include "HeadTracking.h"
#include "../PluginProcessor.h"

namespace headtracking
{
juce::File defaultFile()
{
    if (const auto overridden = juce::SystemStats::getEnvironmentVariable ("AGOGICA_HEAD_TRACKING_FILE", {}); overridden.isNotEmpty())
        return juce::File (overridden);

   #if JUCE_WINDOWS
    const auto base = juce::File::getSpecialLocation (juce::File::windowsLocalAppData);
   #else
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support");
   #endif
    return base.getChildFile ("Agogica").getChildFile ("head-tracking");
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

    map = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readWrite, false);

    if (map->getData() == nullptr || map->getSize() < (size_t) fileSize)
        return;

    record = static_cast<Record*> (map->getData());
    record->version.store (version, std::memory_order_relaxed);
    std::memcpy (record->magic, "AGHT", 4);
}

void Writer::write (const Pose& pose) noexcept
{
    if (record == nullptr)
        return;

    const auto s = record->sequence.load (std::memory_order_relaxed);
    record->sequence.store (s + 1, std::memory_order_relaxed); // odd: writing
    std::atomic_thread_fence (std::memory_order_release);

    record->yaw.store (pose.yaw, std::memory_order_relaxed);
    record->pitch.store (pose.pitch, std::memory_order_relaxed);
    record->roll.store (pose.roll, std::memory_order_relaxed);
    record->timestampMs.store (pose.timestampMs, std::memory_order_relaxed);
    record->flags.store (pose.face ? faceFound : 0u, std::memory_order_relaxed);

    record->sequence.store (s + 2, std::memory_order_release); // even: done
}

Reader::Reader (juce::File f) : file (std::move (f)) {}

void Reader::remapIfNeeded()
{
    const auto newIdentity = file.getFileIdentifier();
    const auto newSize = file.getSize();

    if (map != nullptr && newIdentity == identity && newSize == size)
        return;

    record = nullptr;
    map.reset();
    identity = newIdentity;
    size = newSize;

    if (! file.existsAsFile() || size < fileSize)
        return;

    map = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readOnly, false);

    if (map->getData() != nullptr && map->getSize() >= (size_t) fileSize)
        record = static_cast<const Record*> (map->getData());
}

std::optional<Pose> Reader::read()
{
    remapIfNeeded();

    // Checked on every read: a new file gets its magic just after it appears.
    if (record == nullptr || std::memcmp (record->magic, "AGHT", 4) != 0 || record->version.load() != version)
        return std::nullopt;

    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto before = record->sequence.load (std::memory_order_acquire);

        if ((before & 1u) != 0)
            continue; // a write is in progress

        Pose pose;
        pose.yaw = record->yaw.load (std::memory_order_relaxed);
        pose.pitch = record->pitch.load (std::memory_order_relaxed);
        pose.roll = record->roll.load (std::memory_order_relaxed);
        pose.timestampMs = record->timestampMs.load (std::memory_order_relaxed);
        pose.face = (record->flags.load (std::memory_order_relaxed) & faceFound) != 0;

        std::atomic_thread_fence (std::memory_order_acquire);

        if (record->sequence.load (std::memory_order_relaxed) == before)
            return pose;
    }

    return std::nullopt; // the writer kept us busy; try again next tick
}
} // namespace headtracking

HeadTracking::HeadTracking()
{
    startTimerHz (2); // idle: just notice when an instance wants tracking
}

HeadTracking::~HeadTracking()
{
    stopTimer();
}

void HeadTracking::timerCallback()
{
    pollNow();
}

void HeadTracking::pollNow()
{
    bool wanted = false;
    registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        for (auto* p : members)
            wanted = wanted || p->getParam ("outputMode") > 0.5f;
    });

    if (getTimerInterval() != (wanted ? 10 : 500))
        startTimer (wanted ? 10 : 500);

    constexpr std::uint64_t staleAfterMs = 500;
    const auto pose = wanted ? reader.read() : std::nullopt;
    const auto now = (std::uint64_t) juce::Time::currentTimeMillis();
    const auto isLive = pose.has_value() && pose->face && now - pose->timestampMs < staleAfterMs;

    live.store (isLive, std::memory_order_relaxed);

    // Live: follow the tracker (it already filters). Otherwise ease back to straight ahead.
    const auto current = yawDegrees.load (std::memory_order_relaxed);
    yawDegrees.store (isLive ? pose->yaw : current * 0.9f, std::memory_order_relaxed);
}
