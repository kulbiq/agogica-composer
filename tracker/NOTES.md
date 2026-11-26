# Agogica Tracker: notes

Decisions, history and measurements, newest first.
Nothing here is a rule; `README.md` describes the code as it is.
Measurements name the model and machine they were taken with: several predate the current pose model.

## 2026-11-25: the tracker's own camera capture on macOS

- JUCE's macOS camera takes a still photo per frame (`AVCapturePhotoOutput`), gets it compressed and decodes it again, one after the other: about 15 frames a second from a 30 fps camera, and one failed photo ended the picture for good.
- `CameraSource_mac.mm` reads the video output instead (`AVCaptureVideoDataOutput`, 640x480, BGRA copied straight into a JUCE ARGB image). Windows keeps `juce::CameraDevice`, which reads real video frames there.
- With it the iPhone camera delivers 30 frames a second (seen in the debugging view, M4 Pro, 2026-11-25).
- A MacBook's own camera gives no picture while the lid is closed; a phone offered as a camera needs several seconds to connect (the 4 s retry closed it before it answered: now 10 s, then "No picture").
- Launched from a terminal session of a coding agent, the app got no camera frames at all, while the same build started from Finder did: checks of the camera need a normal launch.

## 2026-11-25: restructuring

- One concrete estimator (the `Engine` interface, engine names and runtime switching served one engine).
- `EngineHost` became `CameraFeed` + `TrackingLoop`; `Window` became `TrackerWindow` + `TrackerPage` + `TrackerSettings`; `PoseOutput` became `ListenerPose` (pure) + `ListenerPoseOutput`.
- The 16 ms writer thread and the page's 60 Hz pose timer are gone: they existed for a smoother that was removed, and the pose only changes with a camera frame or a button.
  If smoothing between frames returns, a timer returns with it.
- The page had its own copy of Invert, Recenter and Pause, so its plot could disagree with what Composer heard.
- Found by running the app: on a Mac with an iPhone offered as camera 0 no frame ever arrived.
  JUCE's macOS camera delivers frames as a chain of still captures (`AVCapturePhotoOutput`), and one failed capture ends the chain.
  `CameraFeed` now reopens a silent camera or tries the next one; a capture path of our own (`AVCaptureVideoDataOutput`) would be the real cure.
- First model load on an M4 Pro took 7.6 s (CoreML compiling), later loads about 1.5 s; the first frame another 2.5 s.
- Open: the timestamp in the file is the time of writing.
  Making it the frame's capture time would let Composer see latency, but shipped plugins call a pose stale after 500 ms, and a slow PC (about 9 frames a second) would come close to that; measure on Windows first.
- `COMPOSER_TRACKER_DIRECTML` (ONNX Runtime 1.24.4 + DirectML) was removed; it is in git history (last in 4c7a5b4).

## Before the restructuring (moved from CLAUDE.md as it stood; file and class names are the old ones)

- Agogica Tracker app (`tracker/`, CMake target `ComposerTracker`, macOS and Windows): JUCE window + web page (`tracker/ui`, Vite) for status, dial (grid every 30°), readouts and a 10 s plot.
- The output is native: `tracker/PoseOutput.h` (own 60 Hz timer thread) applies Invert/Recenter/Pause to each frame's reading as it is (no filter, no smoothing since the switch to HFHPE: the HeadFilter/FilterTuner gate, the Stability sliders and PoseSmoother were removed; the research port in `research/head-tracking/headtrack/filters.py` keeps them for comparison) and writes the shared file (`headtracking::Writer`, `Source/stage/HeadTrackingFile`, shared with the plugin); the page only shows it ("pose" events) and sends the buttons.
  Before, the page wrote the file: Composer heard ~8 Hz steps on a slow PC and lost tracking whenever the page was paused.
- Invert is in the app's settings (an old page's localStorage value moves over once).
- The preview is frames from the engine (`EngineHost::setPreview`, ~10 JPEGs a second while the Camera section is open): Windows lets only one client open the camera (getUserMedia: "Device in use", measured), so the page never opens it, on either platform.
- About (Help menu; the app menu on macOS) is a view of the page.
  Icon: `tracker/icon.svg` (the Agogica tile and ictus ring with the dial inside).
- **Engine: HFHPE** (PINTO0309/High-Angle_Robust_Fast_Head_Pose_Estimation, 2026; the joint ViT-T 128 graph `hfhpe_vitt_ypr_kappa_1x3x128x128.onnx`: yaw/pitch/roll as (cos, sin) + a von Mises kappa per angle; full 360° yaw; code MIT, trained without non-commercial data, **weights' licence not stated - confirm with the author before shipping**) on PINTO's YOLOX Body-Head-Hand head box put through HFHPE's crop contract (`Decode.h` `hfhpeCrop` + `squareToTensor`: +5 % per side, long-side square, border repeated, area resize to 128, RGB, x/127.5-1; matches the reference's OpenCV output, tested).
  HFHPE's own DEIMv2 detector gives readings within ~0.6° but costs ~250 ms/frame on the i5.
  Yaw sign as before (+ = turned to the person's right), so Invert settings carry over.
  `confidence` = kappa/(kappa+5), shown in the plot, filters nothing.
  Replaced 6DRepNet360 (ResNet-50, trained on non-commercial 300W-LP/CMU Panoptic; ~90 ms pose, ~9 fps here).
  ONNX Runtime 1.30 (arm64: CoreML EP with `RequireStaticInputShapes` and a model cache in `~/Library/Caches/Agogica/coreml` - not yet run with HFHPE on a Mac).
  A failing frame counts as "no head", never stops the engine.
- Windows: ONNX Runtime win-x64 on the CPU (2 threads, `session.set_denormal_as_zero`, no spinning - measured with 6DRepNet360: denormals made pose 4x slower, spinning pools another 40 %). i5-10210U, Release, first run: detector ~19 ms + crop ~1.3 ms + HFHPE ~10 ms alone (~32 ms measured in the app while other work loaded the machine); this laptop's camera delivered ~15 fps through JUCE. Research measured HFHPE at 25 fps ≈ 1 core; ONNX Runtime 1.30 and 1.32 time it the same; OpenVINO gains nothing on this CPU. Dev option `COMPOSER_TRACKER_DIRECTML` (ONNX Runtime 1.24.4 + DirectML) is from the 6DRepNet360 days, not re-checked.
- `engines/StillGate.h`: while the head box hasn't changed (16x16 grey thumbnail, mean difference < 4), the last reading is repeated instead of running the models (at least one full estimate a second): ~70 % of frames skipped for a composer looking at the screen (measured with 6DRepNet360).
- Native, not onnxruntime-web: open WebKit 26 bug (runaway CPU/memory).
  Models: `tracker/models/fetch_models.py` (YOLOX from PINTO's zoo, HFHPE from its GitHub release; on Windows run it with `uv run --no-project python` or put the two .onnx files there by hand; the build only checks them), sources/licences in `MODELS.md`.
  Dev switches: `AGOGICA_TRACKER_LOG`, `AGOGICA_TRACKER_EP=cpu|dml`, `AGOGICA_TRACKER_PROFILE=<folder>` (ONNX Runtime times every node; a JSON per model on quit; it roughly doubles inference time, so read it as proportions).
  Monitoring in View > Debugging and the log, once a second: process CPU in cores, frames the models ran on, reused and dropped.
- Research (`research/head-tracking`, Python via uv, never shipped): records the camera with frame times, runs the models (6DRepNet360, HFHPE) and a port of the former HeadFilter/FilterTuner/PoseSmoother plus StillGate over a recording, and simulates the engine loop at any model time per frame (`compute_ms`) or resolution; `analiza.ipynb` (frame rate vs the needle's path, current model vs HFHPE, side-by-side videos) and `eksperymenty.ipynb` are generated by `tools/make_notebook.py`.
  HFHPE (PINTO 2026, full 360°, no non-commercial training data, weights' licence unclear) runs on the Tracker's own YOLOX head boxes at ~35 ms/frame here (its DEIMv2 detector: ~248 ms, readings within ~0.6°); `tools/fetch_hfhpe.py`.
  Windows Smart App Control blocks uv's per-venv `python.exe` trampoline: `.venv` is made by `tools/make_venv.py`.
  `headtrack/filters.py` is the former C++ filter chain (removed from the app), kept for comparisons; `tests/test_port_behaviour.py` holds its old C++ unit tests.
  Recordings stay local (faces).
  README lists alternative head-pose models with licences.
- App Nap is disabled.
  No longer always on top: with the output native, a paused page doesn't stop tracking (macOS not re-checked since).
- View > Debugging (menu bar, saved): off = a fixed 380 px window with status, dial, buttons and camera, its height fitted to the page (native `fitHeight`); on = readouts and plot in a resizable window (size remembered separately, kept on screen).
- The window is made resizable once: `setResizable` later recreates the native window and WebView2 stops drawing into it; the compact view is fixed by size limits, which JUCE applies to the window with its frame (`fitCompact` adds it).
- Windows build: a folder (`Agogica Tracker.exe`, `onnxruntime.dll`, `ui\`, `models\`), WebView2 profile in `%LOCALAPPDATA%\Agogica\Tracker\WebView2`; it runs from the build folder - install folder and `startTracker` on Windows are still to do.
- Every build installs it per user to `~/Applications/Agogica Tracker.app`; Composer's panel (Binaural, not live) shows a "Start head tracking" chip (`HeadTracking::startTracker`: the installed copy first, then `open -b com.agogica.tracker`).
  After rebuilding, quit a running tracker first: `open` only re-activates a running instance.

## Before the Tracker ships (review of 2026-11-27, not fixed: the Tracker is in no release yet)

Found by reading the code, not reproduced: each needs its reproduction first.

- Closing a camera can hold up the window for seconds: `CameraSource_mac.mm` stops the session with `dispatch_sync` on the capture queue, on which a slow `startRunning` (a phone camera) may still be waiting. Reached from choosing another camera, the 10 s "no picture" fallback and quitting.
- A denied camera permission shows only "No picture" after 10 s: check `authorizationStatusForMediaType` and say where to allow it.
- The tracking thread has one autorelease pool for its whole life (JUCE's `Thread::run`): whatever ONNX Runtime's CoreML provider autoreleases per frame stays until the thread ends. Wrap the loop body in a pool and watch memory over 30 minutes.
- Windows: the settings file is in the roaming `%APPDATA%\Agogica`, everything else of Agogica's in `%LOCALAPPDATA%` (`Source/stage/AgogicaFolder.h`); camera ids and window places shouldn't roam.
- `Tracker-crash.log` grows without limit, and a backtrace names the user's folders: it must never be sent anywhere.
- The page: the plot's drawing loop runs while the debug view is hidden, and Pause is always off after the page reloads.
- Windows: the camera menu binds every camera filter each time it opens (may disturb the one in use).
- The head-tracking file: on Windows nothing stops a second writer (macOS: `flock` in `headtracking::Writer`).
- The weights of the head-pose model still have no licence (`tracker/models/MODELS.md`): the reason the Tracker is in no release.
