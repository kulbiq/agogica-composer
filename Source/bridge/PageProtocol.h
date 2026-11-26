#pragma once

// What the editor and its page say to each other, by name. The page's side is
// ui/src/bridge.ts: the two files are changed together.
namespace page
{
// The page calls these.
inline constexpr auto getStage = "getStage";           // () -> the stage (bridge/StageSnapshot)
inline constexpr auto edit = "edit";                   // ({ phase, target, param, value }) one step of a gesture
inline constexpr auto setName = "setName";             // (id, name)
inline constexpr auto setIcon = "setIcon";             // (id, icon key from ui/src/icons.tsx, "" = automatic)
inline constexpr auto setScene = "setScene";           // ("A".."H")
inline constexpr auto startTracker = "startTracker";   // () -> whether Agogica Tracker was started
inline constexpr auto getBuildInfo = "getBuildInfo";   // () -> { version, format, host, credits, debug? }
inline constexpr auto setEditorSize = "setEditorSize"; // (width, height) from the page's resize grip

// The editor sends these.
inline constexpr auto stage = "stage";     // the stage, when it changed
inline constexpr auto levels = "levels";   // every track's input level in dB by id, 30 times a second
inline constexpr auto headYaw = "headYaw"; // the listener's head in degrees, when it moved
inline constexpr auto load = "load";       // DSP load of this track and of all, about 4 times a second
} // namespace page
