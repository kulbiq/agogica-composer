# Composer by Agogica

Composer is an audio plugin for **topophonic composition**: every player (instrument, track) stands somewhere in a virtual room, and you hear the ensemble as it would sound from the listener's seat.
It runs inside any DAW (developed against Dorico 6 and Ableton Live 12) as VST3 and AU, with a web-based editor.

Detailed engineering notes, decisions and traps live in [CLAUDE.md](CLAUDE.md); this file is the overview and the roadmap.

## Current approach

### One plugin per track, one shared stage
- Each track gets its own Composer insert; that instance is one player.
- All instances in the same host process find each other through an in-process registry, so **every editor shows the whole ensemble** and any player can be moved from any editor.
  A move always writes the owning track's parameters, so automation lands on the right track.
- Room size, listener position, output mode and the Distance level setting are shared: each instance keeps its own copy (so exports and project reloads stay correct), and the UI writes changes to all of them.
- **Scenes A-H** are alternative layouts of the whole ensemble (positions, room, listener); switching scenes stores the current layout and recalls the other one.
- **Player names** come from the host's track name where the host sends one (VST3 channel context, e.g. Ableton Live, REAPER, Cubase).
  Dorico and MuseScore can't give the instrument name to an effect plugin, so there each player is named by hand ("Edit player", or double-click the name):
  - Dorico sends a name it generates itself ("Track N"): it doesn't know which instrument a mixer channel plays (Steinberg on their forum, 2022).
  - MuseScore 4 sends nothing (its VST3 host in `musescore/muse_framework` never calls `IInfoListener::setChannelContextInfos`), so players show as "Player N".
  - AU has no track name at all.
  Possible later: reading the players through Dorico's Remote Control API, a MuseScore plugin that sends the part names, or a MuseScore pull request that passes the track name; all of them still have to match names to instances.

### Sound
The DSP is a chain of independent stages (`Source/dsp`), each reading the player's input history and adding its output:
- **Direct path**: delay by distance (speed of sound, no Doppler: a moving player's delay settles and crossfades instead of gliding), loudness by distance (-2 dB per doubling, adjustable as "Distance level" in the settings), then placement.
- **Placement**: Stereo (constant-power pan, rear positions mirrored to the front) or **Binaural** (HRTF from the SADIE II KU100 dummy head, full 360°, for headphones).
- Early reflections, the reverb tail, halls and air absorption were removed to save CPU (2026-11); they are in git history and come back rebuilt for CPU.

### Head tracking
- The **Agogica Tracker** app (`tracker/`) watches the listener through the camera and shares the head's rotation with every Composer instance through a small memory-mapped file (no network).
- It finds the head with a YOLOX head detector and reads its direction with **HFHPE** (a full 360° of yaw), both run natively by ONNX Runtime (CoreML on macOS).
  Each camera frame's reading goes to Composer as it is: there is no filter and no smoothing.
- `tracker/README.md` describes the app.
- In Composer, head rotation turns the scene in Binaural mode during live playback only; exports never depend on the camera.

### UI
- The plugin editor is a web app (`ui/`, React + Vite) embedded in the plugin and served offline; the tracker's page lives in `tracker/ui`.
- Styling follows the Agogica design system (cool slate monochrome, Space Grotesk / Hanken Grotesk / JetBrains Mono, bundled offline).

## Build

Requirements: CMake ≥ 3.22, Xcode (macOS), Node.js, Python 3; the JUCE submodule (`git submodule update --init`).

```sh
cmake --preset debug && cmake --build --preset debug      # plugin (VST3, AU, Standalone), UI, tests
ctest --preset debug                                      # unit tests
scripts/pluginval.sh                                      # auval + pluginval (strictness 10)
cmake --preset release && cmake --build --preset release --target ComposerTracker   # tracker app alone (not installed)
```

A full build installs per user, without admin rights: the plugins into `~/Library/Audio/Plug-Ins`, the tracker into `~/Applications` (target `ComposerTrackerInstall`).
After rebuilding, quit the DAW completely (Dorico's audio engine keeps the old plugin loaded) and quit a running tracker before starting the new one.

## Requirements

macOS 11 or newer on Apple Silicon (VST3 and Audio Unit), Windows 10 or 11 (VST3).
Tested in Dorico 6, MuseScore 4 and Ableton Live 12.

## Privacy

Composer works offline. Official releases ask once whether you want to share anonymous usage data; nothing is sent without a yes, and builds from source never ask.
What is sent, where it goes and how to turn it off: `PRIVACY.md`.

## Licence

Composer is free and open source under the GNU Affero General Public License, version 3 (`LICENSE`), JUCE's open-source licence.
What the plugin contains from others, and under which terms, is listed in `THIRD_PARTY_NOTICES.md`; the release zips carry both files.
The SADIE II HRTFs are Apache-2.0 (`Resources/hrir/`, attribution shown in the UI).

To resolve before the tracker ships: the weights of **HFHPE** (the tracker's head-pose model; code MIT) carry no stated licence, so the author must confirm the terms first (`tracker/models/MODELS.md`).
The YOLOX head detector is Apache-2.0.
Until then the tracker is built for development only and is never part of a release.

## Roadmap

Ideas and decisions collected during development, so none of them get lost.

### Workflow and UI
- **One Composer interface per DAW project** instead of one editor per track: a single window showing and editing the whole stage, while each track still carries its own instance for the sound.
- **Scene automation**: switch scenes from the DAW timeline (a shared, automatable scene parameter), later with **transitions** where players glide from one scene's positions to the next.
- **Movement synced with the score**: player paths drawn on the map and tied to bars and beats (host tempo and position), so players can walk through the room with the music.
- Scene export/import (a file with all positions and scenes) to move a layout between Dorico and other DAWs.
- Separate stages for several projects open in one host process (e.g. Reaper tabs); today they would share one stage.
- Player label placement when players crowd together (names can overlap).

### Sound
- **Speaker arrays**: a Speakers output mode (quad, 5.0, octophony ring, custom angles) with vector-based amplitude panning (VBAP), plus a binaural "virtual speakers" preview so speaker pieces can be composed on headphones.
- **Multichannel output from Dorico**: its mixer is stereo, so a Composer output engine could open its own audio device (e.g. Loopback, BlackHole or the interface itself) and send the speaker feeds there.
  It needs drift/jitter handling between the two audio clocks, and recording happens in the receiving app.
- Re-tune the distance law with the reflections in place (the direct path may sound right steeper than -2 dB per doubling).
- Bake the preferred Pro panel values into the hall presets.
- HRTF choice: several dummy heads / human subjects, and SOFA import for personal HRTFs.
- Elevation (players above or below the listener) once the map is 3D.

### Head tracking
- **AirPods head tracking** (AirPods Pro, AirPods Max, AirPods 3rd gen and later): Core Motion's `CMHeadphoneMotionManager` (macOS 14+) gives the head's rotation from the headphones' own sensors, with no camera, full 360° and no front/back ambiguity.
  The tracker app would read it and write the same shared file, so Composer itself doesn't change; the camera stays as the fallback for other headphones.
  Needs the motion permission (`NSMotionUsageDescription`) and a recenter on start, because the headphones report rotation relative to where they started.
- Smoothing between camera frames on slow computers, if the steps are audible.
- Measure the full motion-to-sound latency.
- Windows (and Intel Mac) builds of the tracker.

### Release
- macOS universal builds (arm64 + x86_64); Authenticode signing on Windows.
