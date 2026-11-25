# Composer by Agogica

Composer is an audio plugin for **topophonic composition**: every player (instrument, track) stands somewhere in a virtual room, and you hear the ensemble as it would sound from the listener's seat.
It runs inside any DAW (developed against Dorico 6 and Ableton Live 12) as VST3 and AU, with a web-based editor.

Detailed engineering notes, decisions and traps live in [CLAUDE.md](CLAUDE.md); this file is the overview and the roadmap.

## Current approach

### One plugin per track, one shared stage
- Each track gets its own Composer insert; that instance is one player.
- All instances in the same host process find each other through an in-process registry, so **every editor shows the whole ensemble** and any player can be moved from any editor.
  A move always writes the owning track's parameters, so automation lands on the right track.
- Room size, listener position, output mode, hall and the Pro settings are shared: each instance keeps its own copy (so exports and project reloads stay correct), and the UI writes changes to all of them.
- **Scenes A-H** are alternative layouts of the whole ensemble (positions, room, listener, hall); switching scenes stores the current layout and recalls the other one.

### Sound
The DSP is a chain of independent stages (`Source/dsp`), each reading the player's input history and adding its output:
- **Direct path**: delay by distance (speed of sound, no Doppler: a moving player's delay settles and crossfades instead of gliding), loudness by distance (-2 dB per doubling, adjustable), air absorption (ISO 9613-1), then placement.
- **Placement**: Stereo (constant-power pan, rear positions mirrored to the front) or **Binaural** (HRTF from the SADIE II KU100 dummy head, full 360°, for headphones).
- **Early reflections**: image sources of the square room (walls, floor, ceiling, corners), diffused and filtered per surface so they don't comb-filter.
- **Reverb tail**: an 8-line feedback delay network per hall preset (Dry, Studio, Chamber, Concert hall, Church), level independent of distance.
- **Pro panel**: project-wide adjustments on top of the hall (reflections, diffusion, floor, brightness, reverb, decay, pre-delay, distance law, air).

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

## Licences to resolve before a release
- **HFHPE** (the tracker's head-pose model; code MIT): its weights carry no stated licence, so the author must confirm the terms before the tracker ships (`tracker/models/MODELS.md`).
- The YOLOX head detector and the SADIE II HRTFs are Apache-2.0 (attribution shown in the UI).
- The repository may later be published under AGPLv3; all dependencies are chosen to be compatible.

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
- Windows (and Intel Mac) builds of the tracker; bundling it with the planned Agogica hub app.

### Release
- Beta build expiry and licence check entry point, CI builds for macOS (universal) and Windows, signing and notarisation.
