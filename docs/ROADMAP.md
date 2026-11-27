# Roadmap

Ideas for later, in no promised order.
What exists today is in the [README](../README.md); how it is built, in [DEVELOPMENT.md](DEVELOPMENT.md).

## Workflow and UI
- **Scene automation**: switch scenes from the DAW timeline (a shared, automatable scene parameter), later with **transitions** where players glide from one scene's positions to the next.
- **Movement synced with the score**: player paths drawn on the map and tied to bars and beats (host tempo and position), so players can walk through the room with the music.
- Scene export/import (a file with all positions and scenes) to move a layout between Dorico and other DAWs.
- Separate stages for several projects open in one host process (e.g. Reaper tabs); today they would share one stage.
- Player label placement when players crowd together (names can overlap).

## Sound
- **Speaker arrays**: a Speakers output mode (quad, 5.0, octophony ring, custom angles) with vector-based amplitude panning (VBAP), plus a binaural "virtual speakers" preview so speaker pieces can be composed on headphones.
- **Multichannel output from Dorico**: its mixer is stereo, so a Composer output engine could open its own audio device (e.g. Loopback, BlackHole or the interface itself) and send the speaker feeds there.
  It needs drift/jitter handling between the two audio clocks, and recording happens in the receiving app.
- **Early reflections and a reverb tail**, rebuilt for CPU (they existed and were removed: 32 binaural players at 96 kHz were too much for a laptop), then the distance law re-tuned with them in place.
- HRTF choice: several dummy heads / human subjects, and SOFA import for personal HRTFs.
- Elevation (players above or below the listener).

## Head tracking
- **AirPods head tracking** (AirPods Pro, AirPods Max, AirPods 3rd gen and later): Core Motion's `CMHeadphoneMotionManager` (macOS 14+) gives the head's rotation from the headphones' own sensors, with no camera, full 360° and no front/back ambiguity.
  The tracker app would read it and write the same shared file, so Composer itself doesn't change; the camera stays as the fallback for other headphones.
  Needs the motion permission (`NSMotionUsageDescription`) and a recenter on start, because the headphones report rotation relative to where they started.
- Smoothing between camera frames on slow computers, if the steps are audible.
- Measure the full motion-to-sound latency.
- Windows (and Intel Mac) builds of the tracker.

## Release
- macOS universal builds (arm64 + x86_64); Authenticode signing on Windows.
- **Player names from the host** where it has them: reading the players through Dorico's Remote Control API, a MuseScore plugin that sends the part names, or a MuseScore change that passes the track name to effect plugins.

## The Tracker
- A licence for the head-pose model's weights, or another model, so the Tracker can be part of a release (`tracker/models/MODELS.md`).
- The list in [tracker-history.md](tracker-history.md), "Before the Tracker ships".
