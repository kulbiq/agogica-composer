# Agogica Tracker

A small desktop app (macOS, Windows) that follows the listener's head with the camera and tells every Composer instance where it points, so the ensemble stays put in the room while the head turns.
No network: the page and the models are the app's own files, and video never leaves the computer.

CMake target `ComposerTracker`; the build is `tracker/CMakeLists.txt`.
History and measurements: `NOTES.md`.
Model sources and licences: `models/MODELS.md` (the pose model's weights have no stated licence: check before shipping).

## How a frame becomes sound

```
camera ──frame──> TrackingLoop ──head, as the camera sees it──> ListenerPoseOutput ──pose──> head-tracking file ──> Composer
                       │                                              │
                       └──status, frame and performance reports──> TrackerPage ──events──> the page (tracker/ui)
                                                                      ^
                                    the page's buttons (Recenter, Invert, Pause) ──┘
```

1. `CameraFeed` keeps the camera's latest frame.
2. `TrackingLoop` (its own thread) takes it and finds the head: `HeadPoseEstimator`, or the last observation again while `StillHeadGate` says the head hasn't moved.
3. `ListenerPoseOutput` turns the head the camera sees into the head Composer hears (`ListenerPose`: Invert, Recenter, Pause) and writes it to the file at once.
4. The same answer goes to the page in the frame's report; the page draws it and decides nothing.

## Where things are

| Folder | File | What it is |
|---|---|---|
| `app/` | `Main.cpp` | The application object and the crash log. |
| | `TrackerWindow` | The one window and where the parts meet; the compact and debugging views; menus. |
| | `TrackerPage` | The web view and everything said between app and page: the only C++ with the page's function and event names and their JSON. |
| | `TrackerSettings.h` | What is remembered between runs; the only place with the settings file's keys. |
| | `AppFiles.h` | Where the build puts the page and the models. |
| `tracking/` | `CameraFeed` | The camera and its latest frame; reopens a camera that gives no picture, or tries the next. |
| | `TrackingLoop` | The tracking thread: a short loop over named steps. |
| | `TrackingReports.h` | What the loop reports: status, frame, performance. Plain structs. |
| | `ListenerPose.h` | Pure: camera-frame head to listener-frame head. Invert, Recenter, Pause, "live". |
| | `ListenerPoseOutput.h` | `ListenerPose` plus the file's writer, behind one lock. |
| | `ProcessCpu` | The process's CPU time (for the performance report). |
| `vision/` | `HeadPoseEstimator` | Head detector, then pose model: a frame in, a `HeadObservation` out. |
| | `HeadDetector` | Runs the detector model and picks the head to follow. |
| | `HeadGeometry.h` | Pure maths: detector rows, which head to follow, the pose model's crop, angles, confidence. |
| | `ImageTensor` | Camera frames as model inputs; `FramePixels` is the one place pixels are read. |
| | `OnnxModel` | One model in ONNX Runtime (CoreML on macOS when it takes the model, else the CPU). |
| | `StillHeadGate.h` | Skips the models while the head's picture hasn't changed. |
| | `HeadOrientation.h`, `HeadObservation.h` (in `tracker/`) | The two values that cross module borders. |
| `ui/src/` | `bridge.ts` | The page's side of `TrackerPage.cpp`: typed functions and events. |
| | `main.ts` | Wires events to the modules below. |
| | `dial.ts`, `plot.ts`, `cameraPreview.ts`, `sessionStats.ts`, `dom.ts` | One job each. |
| `models/` | `models.json` | Each model once: name, SHA-256, download. Read by the build, the check and the fetch script. |

Tests of the pure parts are in the plugin's test program: `tests/TrackerTests.cpp`, `tests/ListenerPoseTests.cpp` (`ctest --preset debug -LE slow`).
The app itself needs ONNX Runtime and the models and has no automated test; see "Checking by hand".

## Threads

| Thread | Does | Talks to the others through |
|---|---|---|
| Message thread | Window, page, buttons, opening the camera | - |
| Camera (the OS's) | Delivers frames to `CameraFeed` | a lock around the latest frame, an event |
| Tracking (`TrackingLoop`) | Models, pose, reports | `ListenerPoseOutput`'s lock; `TrackerPage`'s one slot per kind of report |

There are no timers on the path to Composer.
A pose is written when a frame has been processed or a button was pressed.

## The file Composer reads

`Source/stage/HeadTrackingFile.h` is the contract; the plugin's reader and `tools/fake_tracker.py` use the same one.

- 64 bytes, memory-mapped, at `~/Library/Application Support/Agogica/head-tracking` (Windows: `%LOCALAPPDATA%\Agogica\head-tracking`); `AGOGICA_HEAD_TRACKING_FILE` overrides it.
- A seqlock: the sequence number is odd while a write is in progress.
- One writer at a time (so not the app and `fake_tracker.py` together).
- Created once at its final size and then only written in place.
- Yaw, pitch, roll in degrees as the listener's head: yaw clockwise, + = turned right. Composer uses yaw only.
- The timestamp is the wall-clock time of writing; a pose older than `headtracking::staleAfterMs` (500 ms) is stale and Composer eases back to straight ahead.
  Shipped plugins have that number built in.
- When the camera goes silent or the head leaves, "no face" is written once; when the app stalls, nothing is written and the pose goes stale by itself.

## The page

Vanilla TypeScript, built by Vite to static files next to the app.
`pageReady()` is the page's first call: it returns what the page needs to draw itself and starts the events; the latest status is sent again then, so nothing that happened before the page loaded is lost.

| Page calls | |
|---|---|
| `pageReady()` | `{ inverted, debugView, version }` |
| `setInverted(on)`, `setPaused(on)`, `recenter()` | the three buttons |
| `setCameraPreviewEnabled(on)` | while the Camera section is open |
| `fitHeight(pixels)` | compact view: the window becomes as tall as the page |

| App sends | When |
|---|---|
| `status` | the phase changed: loading models, waiting for the camera, compiling, tracking, failed |
| `trackingFrame` | every camera frame processed: found, live, what Composer hears, the camera's angles, head box, timings |
| `performance` | once a second |
| `cameraPreview` | about ten JPEGs a second while enabled |
| `debugView`, `about` | from the menus |

## Development switches (environment variables)

| Variable | Effect |
|---|---|
| `AGOGICA_TRACKER_STILL_FRAME=<picture>` | The picture stands in for the camera, 30 times a second. |
| `AGOGICA_TRACKER_LOG=<file>` | Every status, frame and performance report as a line of JSON. |
| `AGOGICA_TRACKER_EP=cpu` | macOS: the CPU instead of CoreML. |
| `AGOGICA_TRACKER_PROFILE=<folder>` | ONNX Runtime times every node; a JSON per model on quit (roughly doubles inference time). |
| `AGOGICA_HEAD_TRACKING_FILE=<file>` | Another file than Composer's (so a test run doesn't turn a running Composer). |

## Building and running

```sh
cmake --preset debug && cmake --build --preset debug    # also installs to ~/Applications (macOS)
python3 tracker/models/fetch_models.py                  # only if the build couldn't fetch the models
```

Windows has no `python3` by default: put the two `.onnx` files from `models/models.json` into `tracker/models` by hand; the build checks their hashes.
`-DCOMPOSER_TRACKER_SKIP_MODELS=ON` builds without models (CI does this to compile and link on both systems).
Quit a running tracker before starting a rebuilt one.

## Checking by hand

```sh
AGOGICA_TRACKER_STILL_FRAME=face.png AGOGICA_TRACKER_LOG=/tmp/tracker.log AGOGICA_HEAD_TRACKING_FILE=/tmp/head-tracking \
  "build/debug/tracker/ComposerTracker_artefacts/Debug/Agogica Tracker.app/Contents/MacOS/Agogica Tracker"
```

Then: the chip says Tracking, the dial points where the face does, the box sits on the head in the Camera section, Recenter turns the dial to 0, Invert mirrors it, Pause says Paused and Composer goes straight ahead.
With a real camera: cover it (No head), and unplug it (Starting camera, then the next camera).

## Known limits

- macOS: JUCE's camera frames are still captures; a camera that fails them (seen with an iPhone as camera) gives no picture, and the app moves on to the next camera.
- There is no camera chooser: the first camera that delivers frames is used.
- Windows: runs from the build folder; no installer, and Composer's "Start head tracking" does nothing there yet.
- macOS builds are arm64 only (ONNX Runtime ships no universal build).
