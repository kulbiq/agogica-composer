# Developing Composer

Composer by Agogica is an audio plugin for *topophonic composition*: each player (instrument, track) stands somewhere in a virtual room, and the ensemble is heard as from the listener's seat.
It runs inside a DAW or notation program (Dorico, MuseScore, Ableton Live, Cubase, REAPER…) as VST3 and AU, and its editor is a web page embedded in the plugin.

This is the engineering guide: how the code is put together, the rules that are easy to break, and how to build, test and release.
Why things are the way they are, with dates and measurements, is in [HISTORY.md](HISTORY.md); ideas for later are in [ROADMAP.md](ROADMAP.md).

## Ground rules
- **Verify JUCE APIs against the version in this repo, not memory** (JUCE 9.0.2, the submodule `JUCE/`).
  Read `JUCE/examples/Plugins/WebViewPluginDemo.h`, `juce_gui_extra/misc/juce_WebBrowserComponent.h` and `juce_gui_extra/native/typescript/webview-interop/` (npm: `@juce-framework/webview`) before writing web-UI code.
- Product decisions (the sound model, formats, what is shared with whom) are the maintainers': ask in an issue first.
- Small, reviewable steps.
  A change is done when the plugin loads in a host and passes pluginval.
- A bug fix starts with a test (or an end-to-end reproduction) that fails first.
- The project is **open source (AGPL-3.0)**: never commit secrets (certificates, Apple credentials, keys - CI secrets only), only AGPL-compatible dependencies (MIT/BSD/Apache/(L)GPL), and no AAX code (the Avid SDK is under NDA).

## Stack
- C++20, **CMake** (no Projucer), JUCE as a git submodule (pinned tag).
- Formats: **VST3** + **Standalone** (dev), **AU** on macOS.
  No AAX.
- Company `Agogica`, bundle id `com.agogica.composer`, manufacturer code `Agog`, plugin code `Cmps`.
  The version comes from the nearest `vX.Y.Z` git tag, which may carry a pre-release suffix (`v0.1.0-beta.1`): the plugin's own version stays numeric, and `COMPOSER_VERSION_DISPLAY` is what people read (`0.1.0-beta.1`, with `-dev` on any commit that is not the tagged one; an untagged checkout is `0.1.0-dev`).
- Web UI: `ui/` - **Vite + TypeScript + React**, built to static files, zipped and embedded with `juce_add_binary_data`; served through `WebBrowserComponent::Options::withResourceProvider` (no network, no localhost server in release builds).
- Windows: WebView2 via `withBackend(webview2)` + `JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1`; macOS: WKWebView (default).
- Tests: Catch2 (unit, DSP), pluginval (strictness 10) in CI.
  Sanitizers: `cmake --preset asan && cmake --build --preset asan && ctest --preset asan` (ASan + UBSan), same with `tsan`; allocation counting is off in those builds.
  Keep them a pyramid: many fast unit tests of pure logic and single DSP stages, fewer `[integration]` tests (anything that builds a `ComposerProcessor`, or uses files, threads or an editor), very few `[slow]` ones.
  Merge a new check into an existing case (SECTIONs, or a GENERATE table) when it covers the same behaviour.
  `ctest --preset debug -LE slow` runs the fast set (about 1 s with `-j 8`); `ctest --preset debug` runs everything, `[slow]` included (fuzz, silence skip).
  Hidden cases never run in ctest: `build/release/ComposerTests "[perf]" -s` is the CPU check (Release only, skipped in Debug), `"[.interop]"` reads a real tracker's file.
  `tests/data/parameters.txt` pins every parameter's ID, version, range, default and choices (hosts save projects and automation by them), and `tests/data/state-v1.bin` is a project saved by an earlier build: never regenerate it, every later build must load it.

## The model
- **One plugin instance per track** (each instance = one player).
  Listener position and room size are shared in-process - see "Traps" below.
- **Every editor shows the whole stage**: all instances sharing the in-process registry, its own player highlighted.
  Dragging another instance's player writes *that* instance's parameters (with gestures), so automation lands on the owning track.
  Player labels come from the host's track name/colour where available (VST3 `updateTrackProperties`); AU needs manual names.
  Dorico and MuseScore can't give instrument names (see the README, "Player names"): Dorico sends generated "Track N", MuseScore 4 sends nothing. Players are named by hand there.
- **Scenes A-H, not groups**: every track is in every scene; a scene is an alternative layout of the whole ensemble (positions + room/listener), one active for all tracks.
  Switching is a UI action (not automatable for now): each instance stores its parameters in the scene it leaves and recalls the new one (as gestures); an unvisited scene starts as a copy of the current layout.
  Later: transitions between scenes. Known ceiling: one stage per host process (two projects in the same process see each other).
- **Players with several tracks**: tracks that share the saved state key `player` are one player with one place, name and icon (`stage::joinPlayer` / `splitPlayer`); a position edit sent to any of them goes to all (`PlayerOf` in `Source/bridge/StageEdits.cpp`).
- **Output**: Stereo (constant-power pan) or Binaural (HRTF, SADIE II KU100); the listener's direction (`listenerYaw`) turns the Binaural scene, and the tracked head adds to it during live playback only.

## Architecture
```
Source/
  PluginProcessor.*   parameters and saved state (AudioProcessorValueTreeState), the audio callback
  PluginEditor.*      hosts the web page; no UI logic in C++
  Parameters.h        every parameter id, group, range and default, once
  dsp/                the sound (realtime-safe; tested without any GUI)
  stage/              what instances share in one process: registry, scenes, seating, head tracking, state rules
  bridge/             the C++ side of the page: the stage snapshot, edits and gestures, page files
  telemetry/          opt-in anonymous usage data (PRIVACY.md)
ui/                   the editor's page (React + Vite + TypeScript, drawn with WebGL)
tracker/              Agogica Tracker, the camera head-tracking app (its own README)
tests/                Catch2: DSP, processor, stage, tracking, telemetry
scripts/, tools/      release, validation, HRIR tables, development helpers
```
- **Parameters** (automatable) live in APVTS: `roomSize` (5–50 m, step 5), `listenerX`, `listenerY`, `listenerYaw` (−180..180°, Binaural only), and per player `playerX`, `playerY` (metres, ±roomSize/2).
  New parameters go at the end of `params::createLayout` (and of `tests/data/parameters.txt`).
  Every instance carries its own copy of `roomSize`/`listenerX`/`listenerY` (see "The model"); positions are stored unclamped and clamped at use.
  They reach the UI as one `stage` snapshot (`Source/bridge/StageSnapshot.cpp`), sent by the editor timer only when it changed; the UI writes them through one native function, `edit` (begin/change/end gesture steps), never through `WebSliderRelay`.
  A relay binds one parameter of one instance, but the map writes other instances' parameters and stage-wide values, which needs one validated path.
- **Non-parameter data** (player names, icons, colours, UI prefs) goes through `withNativeFunction` (UI → C++) and `emitEventIfBrowserIsVisible` (C++ → UI), stored in the APVTS state tree so it's saved with the session.
- **State**: `getStateInformation` / `setStateInformation` persist the whole ValueTree (params + layout).
  Reopening a DAW project must restore the exact scene.
- **Realtime safety**: no allocation, locks, logging or I/O on the audio thread.
  UI → DSP only via atomics/APVTS; DSP → UI (meters, load) via a lock-free FIFO polled by a 30–60 Hz timer on the message thread, then emitted as events.
- Editor is **resizable** with a sensible minimum; the web UI must reflow.

## The stage map
- **Room**: a fixed **square**, top-down view.
  Its side length (metres) is a stepper in the top bar: 5–50 m in 5 m steps, default 20 m.
  The map always shows the whole square scaled to fit; a subtle grid every 1 m (5 m major lines) and a scale label.
  The Azimuth view draws a circle inscribed in the square with azimuths (0° ahead, +90° right); it is UI only.
- **Coordinates** in metres, origin at the square's centre, +x right, +y "front" (towards the top of the map).
  Everything is clamped inside the square.
- **Players** (tracks) and **one listener** are freely draggable nodes.
  The listener faces +y, turned by `listenerYaw` in Binaural (Q/E on the map with the listener selected; stereo speakers don't turn, so the arrow doesn't either).
- The UI sends **only positions** (player x/y, listener x/y, room size).
  It does no audio maths.
  All acoustic results (pan, delay…) are computed in C++.
- Changing the room size keeps positions in metres; nodes outside the new square are clamped to its edge.
- There are no x/y fields and no derived values (distance, pan, delay) in the UI; the snapshot carries only what the page draws, plus the ranges and defaults of what it has controls for (`ranges`), so the page keeps no copy of `Parameters.h`.
- Dragging writes parameters through `edit` with begin/end gesture steps, so DAW automation records correctly; automation playback moves the nodes.
- Keyboard accessible (arrow keys nudge the selected node 0.1 m, Shift = 1 m), works at 1× and 2× DPI, smooth at 60 fps.

## The sound (`Source/dsp`)
- The processor writes the mono input (a stereo input is summed) into a `History`; `DirectPath` reads it at the player's delay and adds its output. It is the only path today: early reflections, the reverb tail, halls and air absorption were removed for CPU (HISTORY.md).
- `DelayTap`: a delay never glides (no Doppler) and never chases a moving player; it holds until the target is still, then crossfades once between two taps.
- `Placer`: constant-power stereo pan, or the HRTF per ear (`Hrtf`, tables embedded per sample rate; `Fir` convolves both ears, AVX2 or SSE2 picked at run time on x64).
- The position delay is intentional and never reported as latency.
- A track silent for longer than `ComposerProcessor::tailSeconds` (the longest delay, 0.85 s, also reported as the tail) skips all work.
- Delay stretch (2026-11-26, decided: the physical delays sound far too small next to a live ensemble; a physical model doesn't fit here): `Source/dsp/DelayStretch.h`. Delay = physical x a multiplier between Pro `proDelayFrom` and `proDelayTo` (1-4x, default 1x to 3x; 1x and 1x = as before), taken from a smooth value-noise field over the stage (`stretch::spanMetres` 12 m) and the song's time (`spanSeconds` 60 s, the host's playhead time, else samples since `prepareToPlay`), so players close together get nearly the same and a section never plays raggedly.
  A track takes a new value only after a pause (input quiet, absolute -60 dBFS or 50 dB under the phrase's peak, for at least 0.25 s and longer than both the old and the new delay): the delay then jumps (`Acoustics::delayJump`), nothing is sounding in it; while playing it holds. Deterministic: exports equal playback. UI: "Delay stretch from / to" in the settings popover.
- **Block processing (2026-11-15):** the processor writes up to `History::maxBlock` (32) samples into the `History`, then calls each stage's `process (history, left, right, n)` (adds; sample i of the block is n - 1 - i samples old, `History::read (tap, age)`).
  `DelayTap::read (history, out, n)` and the level glide stay per sample inside the block, so they sound exactly as before.
  The HRIR convolution is `dsp::ConvolveTwo` (`Source/dsp/Fir.*`): both ears over a run of outputs, each coefficient loaded once per run; AVX2 + FMA picked at run time (cpuid + xgetbv, intrinsics without `/arch`, so nothing else in the build needs AVX), SSE2 on older x64, plain loops elsewhere (arm64: the compiler vectorises them).
  Every output is summed in the same order whatever the run length, so host block sizes never change the result (`tests/BlockDspTests.cpp`).
  Direction fades of up to `Placer::blendDegrees` (5) convolve once with a per-block blend of the two HRIRs (within 1-2 % RMS of the per-sample fade); larger jumps still fade two convolutions per sample (a per-block blend there adds a zipper the click test catches).
  The HRIR index has hysteresis (`Placer::hysteresisDegrees`, 0.35°): it changes only once the angle is that far past the half-way point, so tracker jitter around x.5° doesn't fade back and forth.
  Stereo no longer writes the HRIR window; going binaural, the mode fade waits one HRIR length for the window to fill. In binaural the stereo pan isn't computed.
  Development load meter: `Source/dsp/LoadMeter.h` (lock-free; realtime blocks only) per instance, shown in the settings popover for all tracks in Debug builds, in Release after Alt+click on the version there.

## How the parts work, and the rules that are easy to break
- Head tracking: `Source/stage/HeadTracking` reads a 64-byte shared file (`~/Library/Application Support/Agogica/head-tracking`, override `AGOGICA_HEAD_TRACKING_FILE`): seqlock, byte layout fixed by static_asserts, created once by atomic rename and only written in place by the tracker (which maps it); the plugin copies the bytes out with ordinary reads. Per process, polled at 100 Hz while any instance is Binaural; the file itself (exists? replaced?) is looked at at most every 500 ms.
  The writer forces the sequence even before each write (a writer killed mid-write leaves it odd).
  Yaw (clockwise = head turned right) rotates the direct sound in Binaural during realtime playback only - exports and Stereo ignore it. It adds to `listenerYaw` (the stage's direction, a parameter, so exports have that part). `tools/fake_tracker.py` stands in for the tracker app (sweep, or `--yaw 90`).
- Agogica Tracker app (`tracker/`, CMake target `ComposerTracker`, macOS and Windows): read `tracker/README.md` first (module map, threads, the page protocol, the file contract, dev switches); history and measurements are in `docs/tracker-history.md`.
  Rules that are easy to break:
  - The head-tracking file's byte layout and `headtracking::staleAfterMs` (500 ms) are built into shipped plugins: never change them, and the tracker must write at least that often while it has a head.
  - One writer at a time, and the tracker never truncates or recreates the file while it runs (plugins shipped before the reader stopped mapping it would crash their host).
  - Invert, Recenter, Pause and "live" are decided in `tracking/ListenerPose.h` only; the page draws what the app sends and computes none of it.
  - Page function and event names exist once per side: `app/TrackerPage.cpp` and `ui/src/bridge.ts`, changed together.
  - The page never opens the camera (Windows allows one reader); the window is made resizable once (WebView2 stops drawing if it is switched later).
  - After rebuilding, quit a running tracker first: `open` only re-activates a running instance.
  - No camera at hand (a MacBook's own camera gives no picture with the lid closed): `AGOGICA_TRACKER_STILL_FRAME=<picture>` runs the whole app on a picture.
- **Usage data (telemetry), opt-in** (PostHog EU, a popup that asks once, four kinds of data).
  `Source/telemetry/Telemetry.*`, one per process (`ComposerProcessor::telemetry`), written to be held to `PRIVACY.md` by `tests/TelemetryTests.cpp`: change the three together, and the popup's list (`ui/src/TelemetryConsent.tsx`).
  Rules that are easy to break:
  - Nothing is collected, written or sent before a yes. The key comes from CMake (`COMPOSER_TELEMETRY_KEY`, passed only by the release script and workflow): without it, or with `DO_NOT_TRACK` set, the build never asks (`stage.telemetry` is `"unavailable"`).
  - The answer and the random install id live in `Agogica/Composer.settings` (next to the head-tracking file, `Source/stage/AgogicaFolder.h`), never in the project state or `localStorage`. Every host and plugin format shares that file: only `setConsent` writes it, and it is read again from disk before anything is kept or sent (a no in one host stops the others).
  - A session is a process in which an editor opened (`editorOpened`): scans, pluginval without a key and offline renders are nothing.
  - No request at shutdown: the session's summary (and whatever a failed request held) is a file in `Agogica/telemetry-pending/`, sent by a later session once it has been left alone for 3 minutes. The sender is one background thread; the destructor cancels a waiting request and the thread ends by itself (the join is longer than the request's timeout).
  - Never names, positions, paths or anything of the Tracker; page errors are cut to 2000 characters, 5 a session, without lines that name a user folder.
  - Sending is C++ only (`juce::WebInputStream`, the system's network stack, `JUCE_USE_CURL=0` stays); the page stays offline.
  - Manual, outside the repo: the PostHog project (EU) must have "Discard client IP data" on; `PRIVACY.md` promises it.
- The page never shows an empty window: `ErrorBoundary` (a render error: message + Reload), "Connecting to Composer…" until the first stage, and "The map can't be drawn" when WebGL is missing or its context is lost (`?nowebgl` in the dev mock). Page errors go to `reportError` (C++ decides whether they go anywhere).
- Loaded project state is held to the page's rules (`Source/stage/StateRules.h`, `sanitise`): names cut, scene A-H, icon and audience checked, non-number parameters dropped. Every read of the state tree holds `stateTreeLock` (a host may load from its own thread); readers get copies.
- The audio thread's parameters are always numbers (`AudioParam::load`: the default stands in for a NaN), and `prepareToPlay` brings the host's sample rate into 8-384 kHz (384 kHz tables take ~1 s to resample in Debug).
- The head-tracking reader no longer maps the file: it copies the 64 bytes out with two positional reads per poll and takes the first when both show the same even count. A file emptied by anything is "no head". The byte layout and `staleAfterMs` are unchanged; on macOS a second writer on the same file doesn't open (`flock`).
- The plugin is built for macOS 11.0 and up (`CMAKE_OSX_DEPLOYMENT_TARGET`, forced), the Tracker for 14.0 (its ONNX Runtime).
- Emitting events before the page has loaded hits the pre-load page (no `window.__JUCE__`) and asserts - start timers from `Browser::pageFinishedLoading`.
- Every UI write is a gesture (`beginGesture` in `ui/src/bridge.ts`), keyboard and typed values included, or hosts may not record automation.
- `Source/stage/StageRegistry` (per-process, `SharedResourcePointer`); every editor shows all instances; room/listener edits fan out to all, player edits land on the owning instance; scenes A-H (`stage::setScene`, slots in the state tree under `scenes`).
  Lifetime rule: other instances are only touched inside `withMembers`/`withMember`, under the registry lock; the processor unregisters first thing in its destructor.
  A fresh insert (state flag `placed` unset) joins the stage via `callAsync` captured by id: copies active scene + room/listener from a placed member and takes a free seat.
- Input levels: every editor shows every track's input (map halo, player-list meter). `ComposerProcessor::readInputLevel` folds the audio-thread peak into a shared level that falls by elapsed time (~300 ms), so many editors can read it; the editor timer sends all of them as one `levels` event (dB by id), separate from `stage` so the stage diff stays quiet.
- One window: a plugin can't close host windows, so an editor opened while another instance's editor is open (`openElsewhere` in the stage snapshot, from `getActiveEditor()`) shows "Composer is already open - <track>" with "Open here anyway"; it unblocks by itself when the other window closes.
- JUCE 9 moved the JS frontend library to `@juce-framework/webview`; `ui/` depends on it via `file:` into the JUCE submodule, so it always matches the pinned C++ side.
- Resizing: the native webview covers JUCE's corner resizer, so the page draws its own grip (`ui/src/ResizeGrip.tsx` → native `setEditorSize`, limits applied in C++).
  Ableton needs it (no host-side resize); Dorico resizes itself but lets the window height collapse below the plugin's 400 px minimum (host behaviour, content is only clipped).

## Visual design
The page follows the Agogica design system; its tokens are in `ui/src/styles/tokens.css`, the room's materials in `ui/src/styles/room.css`.
- Cool slate monochrome: paper `#F1F5F9`, ink `#0F172A`, ink-light `#334155`, ink-lighter `#64748B`, ink-faint `#94A3B8`, surface `#FFFFFF`; hairlines `rgba(15,23,42,.10/.20)`.
  Dark theme inverts the slate (paper `#0B1120`, ink `#E2E8F0`), following `prefers-color-scheme`.
- **Signal Amber `#C2772E` is reserved for the live pulse / synchronization moment only**
  - never on chrome, text or borders.
- Type: Space Grotesk (display), Hanken Grotesk (body), JetBrains Mono (labels, UPPERCASE, tracking .14–.20em).
  **Fonts are bundled** (`@fontsource/*`, woff2 emitted by Vite) - the plugin must work offline.
- Radius: inputs 2px, chips 3px, buttons 0px.
  Motion "breathes rather than ticks": eased oscillation `(1 − cos(2π·t/T))/2`, T ≈ 2.4–5.5 s; one rAF loop, no CSS transitions on canvas; canvas colours come from the `--ink-rgb` token so they follow the theme.
- Use CSS custom properties for all tokens; no hardcoded colours in components.

## Traps & guidelines (read before touching multi-instance, threading or the bridge)
Solutions may differ - these are the failure modes to design against, not fixed recipes.

### Multi-instance shared state (we chose one instance per track)
- **Parallel audio threads**: hosts process tracks concurrently.
  Anything shared between instances is read by several audio threads at once.
  No mutexes, no allocation, no `MessageManagerLock` on the audio thread.
  Shared listener/room state must be lock-free **and read consistently**: pack x/y into one 64-bit atomic or use a seqlock/version counter - never two independent atomics (torn reads).
  Check `is_always_lock_free`.
- **Registry lifetime**: use a reference-counted shared object (`juce::SharedResourcePointer`), not a raw static singleton.
  Instances register/unregister on the message thread; the audio thread only reads the lock-free snapshot, never iterates the registry.
- **Scope**: statics are shared by *everything in one process* - including multiple projects open in the same host (e.g. Reaper tabs).
  Give each instance a **scene/group ID** (user-selectable, saved in state) and only share within the same group.
  (Not done: A-H are scenes, and there is one stage per process - see "The model".)
- **Isolation**: sandboxed hosts (Bitwig sandbox, out-of-process AU, Reaper "separate process") and mixed formats (VST3 vs AU = different binaries) mean instances may **not** share memory at all.
  Detect "I'm alone" and show it in the UI; never crash or silently mis-position.
  Cross-process sharing (shared memory/IPC) is out of scope for v1.
- **Timing across tracks**: hosts may render tracks ahead of time (Cubase ASIO-Guard, Reaper anticipative FX), so "the current listener value" differs in audio time between instances while the listener moves.
  Options: timestamp shared values with the playhead sample position, or accept the error and rely on smoothing.
  Static scenes are unaffected.
  Measure before over-engineering.
- **Listener ownership**: the listener/room must have one authoritative source for automation and project reload (e.g. a "leader" instance, or identical automatable params in every instance).
  Decide explicitly; instance restore order on project load is arbitrary.

### UI ↔ DSP bridge
- **No feedback loops**: editors poll shared/processor state with a 30–60 Hz timer and repaint only when a version number changed.
  Values received from elsewhere are drawn, never written back as parameters.
  Only genuine user gestures call begin/`setValueNotifyingHost`/end gesture (the relays do this for you).
- Never touch the WebView, message thread or `MessageManagerLock` from `processBlock`.
  DSP → UI goes through a lock-free FIFO drained by the editor's timer.
- Only visible editors should have a live WebView/timer work; 32 open WebViews are heavy.

### DSP correctness
- **Offline bounce**: rendering can run faster than realtime with no UI timers running.
  Audio must depend only on parameters/automation and processor state - never on values pushed by the UI or a timer.
- Allocate delay lines in `prepareToPlay` (worst case: largest room diagonal at the max sample rate); handle any block size, including 1 sample and varying sizes; `juce::ScopedNoDenormals` in `processBlock`.
- The position delay is **intentional - do not report it as plugin latency** (`setLatencySamples` stays 0), or the DAW will compensate it away.
  (Possible later option: subtract the smallest player delay so the nearest player has zero delay - ask first.)
- Sample-rate changes: recompute delay in samples from seconds; keep smoothing times in ms.

## Dev workflow
- `ui/`: `npm run dev` serves on localhost; configuring with `-DCOMPOSER_UI_DEV_SERVER=ON` makes Debug builds load the page from the Vite dev server (hot reload inside the plugin/Standalone).
  Release builds always use the embedded zip.
- The Tracker is built only with `-DCOMPOSER_BUILD_TRACKER=ON` (it fetches ONNX Runtime, and its models unless `-DCOMPOSER_TRACKER_SKIP_MODELS=ON`); the plugin's build downloads nothing but Catch2.
- `cmake --preset debug && cmake --build --preset debug` builds Standalone + VST3 + AU and rebuilds the UI zip when `ui/` changes; run in AudioPluginHost and at least one real DAW before calling a feature done.
- Install to **per-user plugin folders** (no admin): macOS `~/Library/Audio/Plug-Ins/VST3` and `/Components`, Windows `%LOCALAPPDATA%\Programs\Common\VST3`. The Tracker installs itself only on macOS (`~/Applications`); on Windows it runs from the build folder for now. The MSVC runtime is linked statically. Building on Windows needs the WebView2 SDK NuGet package where JUCE looks for it: `nuget install Microsoft.Web.WebView2 -OutputDirectory "$env:USERPROFILE\AppData\Local\PackageManagement\NuGet\Packages"`. `COPY_PLUGIN_AFTER_BUILD` does this.


- In a plain browser `npm run dev` uses `ui/src/devMock.ts`, a fake plugin with an orchestra on stage (`?players=64`, `?binaural`, `?3d`, `?settings`, `?nowebgl`, `?telemetry=ask` and more, see the file).
- `scripts/pluginval.sh` runs auval + pluginval (strictness 10) on the installed VST3 and AU.
- `scripts/release-macos.sh` builds the macOS zip (VST3 + AU at the zip root), signs it (`SIGN_IDENTITY`: Developer ID, hardened runtime, timestamp), notarizes (`NOTARY_PROFILE` or `APPLE_API_KEY...`) and staples each bundle, writes the `.sha256` next to it, and with `--publish` attaches both to the tag's GitHub release (`gh`); `NOTARIZE=0` for local tests. `TRACKER=1` adds the Tracker (with `scripts/tracker.entitlements` for the camera) and is never published: the head-pose weights have no licence yet.
  Known: the AU prints `WARNING: Current program is -1` - JUCE AU-wrapper behaviour (AU present preset defaults to -1), not a failure.
- AudioPluginHost: `cmake -S JUCE -B build/juce-extras -DJUCE_BUILD_EXTRAS=ON && cmake --build build/juce-extras --target AudioPluginHost`.
- After rebuilding, quit the host completely (Dorico's audio engine keeps the old plugin loaded).

## Release & CI
- `.github/workflows/check.yml` builds `ComposerTests` and runs `ctest --preset debug -LE slow` on every push to `main` (everything, `[slow]` included, nightly; on a branch only when started by hand, "Run workflow"), then pluginval 10 (and auval on macOS), on macOS and Windows; nothing is signed.
  The Tracker is compiled and linked without its models in a job of its own (`tracker`), which is not required for merging: it depends on ONNX Runtime's download (the NuGet package, which holds the headers and both systems' libraries).
  The Windows job uploads the Debug VST3 for trying on a PC.
- Releases: tag `vX.Y.Z` (a suffix such as `-beta.1` makes a GitHub pre-release); `.github/workflows/windows-release.yml` builds the Release VST3 zip on the tag and attaches it (unsigned) to the GitHub release; `scripts/release-macos.sh --publish`, run on a Mac with the Developer ID, adds the signed and notarized macOS zip from the clean, tagged commit.
  Keep `ui/src/devMock.ts` able to show a full stage: the README's screenshot (`docs/images/composer.png`) is made from it.
- Later: macOS universal (arm64 + x86_64); Authenticode signing on Windows.

- Both release zips carry `LICENSE`, `THIRD_PARTY_NOTICES.md` and `THIRD_PARTY_LICENSES.txt` (gathered by `scripts/third-party-licences.sh`).
- `check.yml` also runs the page's tests (`npm test` in `ui/`), ASan + UBSan on every run and TSan nightly, and `dco.yml`, the one check a pull request must pass before merging, looks at every commit for its sign-off.
