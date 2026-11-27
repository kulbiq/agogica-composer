# How Composer got here

A log of decisions and measurements, kept because the reasons are easy to forget.
It is history: what it describes may have changed since.
What is true today is in [DEVELOPMENT.md](DEVELOPMENT.md).

## The editor's redesign and the WebGL stage (2026-11)
- **UI redesign (2026-11-26)**, built in phases (plan: list on the left; top bar Output · Scene · Room · settings; instrument icons; Grid / Azimuth / 3D map; players with several tracks).
  Phase 1 done: `TopBar`, `PlayersPanel` (double-click or F2 renames), `SettingsPopover`, icons picked from the track name (`ui/src/icons.tsx`, `autoIcon`), Grid/Azimuth map views; view and names preferences in `localStorage`.
  Phase 2 done: per-player `icon` in the state tree (empty = picked from the name; native `setIcon` takes only the keys of `ui/src/icons.tsx`, listed again in `Source/stage/StateRules.h`, the rules a project file is held to on load as well), "Edit player" under the selected row drills the panel into name + icon (`EditPlayer.tsx`).
  Phase 3 done: players with several tracks. Tracks share a saved state key `player` (empty = a player of its own); `stage::joinPlayer` / `splitPlayer` (native `joinPlayer`, `splitPlayer`), and position edits (with gestures), names and icons sent to any track go to all of its player's tracks (`PlayerOf` in `StageEdits.cpp`).
  Joining copies the player's place in every stored scene (`copyScenePlaces`); after a scene switch the first track's place wins if hand-edited lanes made them differ. Splitting keeps the place and goes back to the track's own icon.
  Two names: every track has its own (`name` = override, else the host's; native `setTrackName`), a player with several tracks has its own too (`playerName`, the same on all its tracks). `setName` on a player of its own renames its track. A track keeps the name it showed when it joins another player, and the player keeps its name when tracks leave. Auto icon: player name first, then its tracks' names.
  The UI groups the snapshot by key (`ui/src/players.ts`: a player takes its first track's id); the list shows a count, a tree of tracks, Select + Merge, and drag a row onto another to join (a track out of its player to split).
  Phase 4 done: `listenerYaw` (stage-wide, in scenes; Binaural turns by it plus the tracked head, exports by it alone; snapshot `listener.yaw`).
  Phase 5 done: the 3D view. One camera draws the map and the room (`ui/src/room/camera.ts`: Panini projection with edge subdivision, near-plane clipping; straight down it is exactly the flat map), so Grid/Azimuth ↔ 3D is a 1.7 s swoop to the listener's eyes.
  The room (`room/scene.ts`, its meshes in `gl/roomMesh.ts`, the players in `gl/figureMesh.ts`): boards, panelled walls, doors, windows over radiators, beamed ceiling at 5 m, seated players with chairs and stands (the music faces the player) facing the centre, each badge a pin just over the head (1.45 m) with its name over it, drawn in depth order; laid out from the room size; materials in `styles/room.css` (Composer's own, not synced from the website).
  Controls (`StageMap.tsx`, by `e.code`): W A S D walk (writes listenerX/Y in one gesture per walk), Shift faster, drag / ← → turn (listenerYaw in Binaural; only the view in Stereo, where nothing turns), up/down look is view only, Space seated 1.2 m / standing 1.6 m eyes, Esc back to the map, a click on a badge selects. 3D is session state: the editor always opens on the map. Tuned values live in `view3d` (100° wide, Panini 0.1, ceiling 5 m).
  Not checked yet: whether Dorico and MuseScore pass Space and W A S D to the WebView.
  After the first MuseScore test (2026-11-26):
  - Head tracking switch: the chip in the top bar turns it on and off while Agogica Tracker runs (state property `headTracking`, default on, the same on every track: native `setHeadTracking`; off = neither the sound nor the 3D view follows the head).
  - The audience is painted, not laid out (always shown on the map, faintly): "Audience" at the right end of the map's tools turns editing on and shows its own tools next to it, Select / Paint / Erase (`SeatTool` in `StageMap.tsx`; decision, 2026-11-26): Paint and Erase are brushes (Alt: the other one, Shift 3 x 3), Select picks seats with a rectangle or a click (Shift adds), and then Erase reads "Erase N" and takes exactly those away (so does Delete; Escape lets go) on a 0.75 m grid from the room's centre (`ui/src/audience.ts`, base64 bitmap in the state property `audience`, the same on every track: native `setAudience`, length checked against the page by `audience.test.ts`); seats face the room's centre; none by default; not in scenes; drawn only.
  - The map zooms (wheel or pinch at the pointer, up to 8x, "Fit"; session only) and pans (drag on empty space); several nodes are selected with Shift/Cmd-click or Shift-drag (a rectangle), in the list too, and move together (one gesture per node); two players or more selected show Merge (`ui/src/mapEdits.ts`).
  - The players list is as wide as its dragged edge (200-480 px, saved in `localStorage`).
  - W A S D and turning always move the listener (on the map W is up the map, Shift faster). Move / Spectate is gone (decision, 2026-11-26: it confused more than it helped).
  - Select / Drag (the map's pointer tool, session only, Drag when the editor opens; `Tool` in `StageMap.tsx`): Drag moves a node (all the selected, when it is one of them), pans on empty space, Shift draws a selecting rectangle; Select always draws the rectangle that selects players, a click selects. Shift, Cmd or Ctrl add in both. While the audience is edited its own tools take the drag and Select / Drag is hidden (two Selects side by side would be confusing). No position reset. The view buttons (Grid / Azimuth / 3D) stand on the map without a frame around them.
  - R (and "Face front" in the room's key bar) looks straight ahead: listenerYaw 0 in one gesture in Binaural, else the view's own direction; pitch level.
  - The map tools wrap onto a second row in a narrow window; the map and the room's centre move down under them (`MapFrame.top`).
- **The stage is drawn with WebGL (2026-11-26), one canvas** (`ui/src/gl/`, driven by `stagePainter.ts`).
  Why: with Holst in MuseScore (64 tracks, a 40 m room, 615 audience seats) the Canvas 2D editor took ~155 % of one core in 3D at rest, all of it drawing (cutting the drawing left 4 %; the 30 Hz events from C++ cost ~2 %).
  - Meshes the GPU keeps, rebuilt only when what they show changes: the room (`roomMesh.ts`: floorboards with their seams and far-away average colour worked out per pixel, walls, ceiling, doors, windows, lines of a fixed px width), the audience seats, the players and their shadows (`figureMesh.ts`; the torso and head turn to the eyes in the shader).
    The camera of `room/camera.ts` runs in the shaders (`world.ts`), Panini included; long faces are cut into 0.5 m pieces so they bend; faces crossing the near plane are clipped by the GPU.
  - A frame only orders the players' parts far to near (an index buffer) and builds what is screen-space: the map's grid and rose, the distance arcs (`tris.ts`, paths stroked once via the stencil), halos and rings.
  - The overlay is one draw call (`sprites.ts`): text and the map's badges are drawn once with the 2D canvas into an atlas and copied texel for texel at whole device pixels; in the room a badge is put together (disc and outline in the shader, the icon from a mipmapped sheet) because its size changes with every step.
  - Draw order follows the canvas: room, map lines, vignette, shadows, seats (depth), figures (tested against the seats only), halos and rings, overlay. Changes from the canvas: halos, rings, badges, names and the 3 / 5 / 10 m labels are always over the figures.
  - A frame is drawn only when something in it changed; while the camera stands still, everything under the halos is kept as a picture (`copyTexSubImage2D`) and copied back. Moving frames are capped at 60 Hz (a 120 Hz screen would draw twice as many), resting ones at 30 Hz.
  - The players list's level meters are drawn on one canvas over the list (`LevelMeter.tsx`, `MeterLayer`): changing a transform per meter 30 times a second made the browser re-layer the whole page each time (`PaintArtifactCompositor::Update`, ~135 ms/s by itself in MuseScore).
  - A lost WebGL context (GPU reset, sleep) rebuilds everything when it comes back.
  Measured in MuseScore with that project (the editor's processes, % of one core): 3D at rest ~155 % before, ~33 % after; walking and turning ~53-68 %; with no stage frames at all ~17 % (events, meters, the host's side).
  To measure: `?players=64&binaural&room=40&audience=600` in `npm run dev` (`devMock.ts`) reproduces the load in a browser; in a host, start it with `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222` (Windows) to reach the editor's page with DevTools.
  The editor's 30 Hz timer costs the host's message thread ~2.7 ms per tick with 64 tracks (`ComposerTests "CPU: what the editor*" -s`), mostly the stage's JSON built to compare with what the page last saw.

## Sound and CPU
- M3 built: `Source/dsp/Spatializer` (Lagrange-3 fractional delay; delay never glides (no Doppler) and never chases a moving player (chained fades flutter): it holds until the target is still for 100 ms, or has moved for 0.5 s, then one 80 ms crossfade between two taps; constant-power pan with 20 ms smoothing, stereo summed to mono); processor reads its own params via cached atomics (a String lookup would allocate); tail 0.25 s, latency 0; tests incl. zero allocations in `processBlock`.
- M3b built: `outputMode` Stereo / Binaural (shared like room, not part of scenes). Binaural = HRTF per player: direct 256-tap FIR (at 48 kHz) per ear from `Source/dsp/Hrtf` (1-degree horizontal tables), 20 ms fades on direction and mode changes; 32 players ≈ 4 % of a core at 48 kHz, 16 % at 96 kHz (measured then; today's CPU check is the whole-chain `[perf]` case below).
  HRTF data: SADIE II KU100 (University of York, Apache-2.0) - `Resources/hrir/` (NOTICE + licence; attribution shown in the UI). Regenerate with `uv run tools/make_hrir.py <dir with D1_*.sofa>`; tables are embedded per sample rate (44.1/48/88.2/96/176.4/192 kHz), no runtime resampling.
  SOFA azimuth is counter-clockwise (+90 = left); the script converts to Composer's clockwise convention.
- M3c built: distance loudness -2 dB per doubling, unity at 1 m (`geometry::distanceGain`, exponent 1/3 - we found the free-field -6 dB far too steep on a dry signal; revisit when reflections exist); glides over 50 ms, applied before pan/HRTF; panel shows Level. There was also an air absorption low-pass (ISO 9613-1 at 8 kHz, Pro "Air absorption"); removed 2026-11-15, in git history.
  2026-11-26: any loss of level with distance sounds wrong (a forte trumpet across the hall must still carry), so `proDistanceLaw` defaulted to 0 dB; then (same day) we set the defaults to -2.5 dB per doubling and a stretch of 1x to 3x. A project saved before a parameter existed loads with what it had then (`params::asBeforeTheyExisted`: -2 dB, no stretch), so it sounds as it did.
- **The DSP was a chain of stages** (M3d; today `DirectPath` is a plain member): `ComposerProcessor::stages` was a `std::tuple` of stages; each has `prepare (sampleRate)`, `setAcoustics (const Acoustics&)` (once per block) and `process (const History&, float* left, float* right, int n)` which **adds** its output (block processing below).
  The processor writes the mono input into `History`; every stage reads it at any delay (no read pointer), so stages are independent of each other and of their order. Plug a stage in or out by editing the tuple.
  Building blocks: `DelayTap` (settle-then-fade delay, no Doppler/flutter), `Placer` (stereo pan or HRTF with crossfades), `DspUtil.h`.
  Stages: `DirectPath` only, for now (stage 1 below).
  Every stage also has `jumpToTargets()`: a track silent for longer than `ComposerProcessor::tailSeconds` (also reported to the host as the tail length) skips all work and jumps to the current position when sound returns.
- **Stage 1 of the CPU work (2026-11-14): the chain is `DirectPath` alone.**
  32 binaural players at 96 kHz took 7 of 8 cores of the Windows laptop in REAPER, and reflections + tail were most of it.
  Then (2026-11-15) everything unused went too: `EarlyReflections` (10 image sources, diffused and filtered per surface), `ReverbTail` (8-line FDN), the `hall` parameter and its UI picker, the Pro reflection/reverb parameters and sliders, the short HRIRs, and their tests.
  Git history has them for when reflections come back, rebuilt for CPU; a project saved with them still loads (unknown parameters are ignored).
  The tail is now the longest delay (`History::maxDelaySeconds`, 0.85 s: the largest room's diagonal stretched 4x); with the reverb it was 10 s (Church at Pro decay x2).
  CPU before this (whole chain, release, `ComposerTests "[perf]" -s`): 32 players ≈ 11 % stereo / 22 % binaural of one core at 48 kHz; a turning head ~1.5x in binaural: 33 % at 48 kHz, 104 % at 96 kHz (M4 Pro, 2026-11-12).
  Windows (i5-10210U laptop on mains power, 2026-11-14, median of 3): 40 % stereo / 75 % binaural / 128 % binaural with a turning head at 48 kHz, 81 / 212 / 375 % at 96 kHz.
  The direct sound alone, still one sample at a time (same laptop, one run): 3.0 / 12.8 / 23.4 % at 48 kHz, 6.1 / 46.0 / 88.2 % at 96 kHz.
  CPU (same laptop, median of 3): 2.2 / 3.8 / 5.3 % at 48 kHz, 4.4 / 10.5 / 16.3 % at 96 kHz (stereo / binaural / turning head).
  For the record: the SSE2 kernel gives 7.5 / 9.5 % binaural / turning at 48 kHz and 26.1 / 32.0 % at 96 kHz; fading two convolutions on every direction change instead of blending, 6.5 / 18.1 % turning.
  Earlier fixes: MSVC kept the old per-sample `dsp::dot` scalar, so x64 MSVC got SSE by hand (an AVX2 dot product measured slower: short sums, then a horizontal add per output); `DelayTap` keeps its Lagrange weights (`History::Tap`) while a delay holds.
- Pro panel (M3e): project-wide `pro*` parameters (shared, saved, not in scenes) -> `Acoustics::adjust`; since stage 1 `proDistanceLaw`, then `proDelayFrom` / `proDelayTo` (delay stretch).
  At the values it shipped with (law -2 dB, no stretch) it sounds exactly like the direct sound as it shipped (`tests/LegacyDirectPath.h`). UI: "Distance level" in the settings popover (`ui/src/SettingsPopover.tsx`, the generic `ParamSlider`); it is one value for the whole plugin (in `ofStage`, fans out).
- When reflections come back: as plain copies of the direct sound they sounded "too filtered" to us; diffusion (two allpasses each) and per-surface filtering fixed it, and a test kept the early colouration < 8 dB.
  Short HRIRs for them were 96 taps at 48 kHz: 64 kept only ~8 % of the far ear's energy at ±90°.
- **Live changes are heard as late as the host buffers** (measured in REAPER 7.86 on the Windows laptop, 2026-11-15): a JSFX after Composer published the block it processed, compared with REAPER's audible position.
  REAPER's defaults processed 313 ms ahead (anticipative FX, 200 ms render-ahead, plus the WaveOut device's ~100 ms), the same with 16 or 32 instances; drags and head turns are heard that late.
  Preventing anticipative FX on the tracks (`I_PERFFLAGS` bit 2, "Prevent anticipative FX") brought it to 121 ms, the rest being the WaveOut buffer; DirectSound with 3 × 512 samples then gave 88 ms (62-104).
  On that laptop, used over Remote Desktop, WASAPI won't open (no input device in the session, even with 0 input channels), and RDP audio adds its own delay on top.
  Composer's own path is fast: the UI's `edit` round trip took 0.7 ms with 32 instances, the map showed the change ~25 ms later (editor timer), and the sound turns within 20 ms.

## Milestones as they were reached
- M0 done: CMake skeleton, VST3/AU/Standalone, embedded UI zip, auval + pluginval 10 pass, checked in Dorico 6 and Ableton Live 12.
- M1 done: `roomSize` param ↔ UI slider, native `getBuildInfo`, C++ → UI `inputLevel` event (atomic peak, 30 Hz timer started on `pageFinishedLoading`), dev-server mode, Catch2 tests.
  Checked in Dorico 6 (bridge, level); after a rebuild, DAWs must be fully quit - Dorico's VSTAudioEngine keeps the old binary loaded.
- M2b done.
- M3 done.
- M3b done.
- M2a done: stage map (`ui/src/StageMap.tsx`) + side panel for this instance's player, listener and room; `dsp/Geometry.h` derived values; `npm run dev` in a plain browser uses `ui/src/devMock.ts` (6 fake players).
