# Composer - instructions for coding agents

Composer by Agogica is an audio plugin (JUCE 9, C++20, VST3 and AU) for *topophonic composition*: each track's player stands somewhere in a virtual room and is heard from the listener's seat.
Its editor is a web page (`ui/`, React + TypeScript, drawn with WebGL) embedded in the plugin; `tracker/` is a companion camera head-tracking app.

**Read [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) before changing anything**: the architecture, the rules that are easy to break, testing, building and releasing are there, and it is the same guide human contributors use.
Why things are as they are: [docs/HISTORY.md](docs/HISTORY.md). Ideas for later: [docs/ROADMAP.md](docs/ROADMAP.md). The Tracker: `tracker/README.md`.

## Ground rules
- **Verify JUCE APIs against the version in this repo, not memory** (`JUCE/`, tag 9.0.2): read the headers and `JUCE/examples/Plugins/WebViewPluginDemo.h` first.
- Ask before making product decisions that the documents do not cover (the sound model, formats, what is shared with anyone).
- Small, reviewable steps; one commit does one thing, and its message is an area, a colon and a sentence that says what is true now.
- A bug fix starts by reproducing the bug, end to end where possible, with a test that fails first.
- A change is done when the tests pass (`ctest --preset debug`, `npm test` in `ui/`), the sanitizers are clean for C++ changes (`asan`, `tsan` presets), and the plugin passes `scripts/pluginval.sh`.
- No compiler warnings, failing or flaky tests are left behind, whoever caused them.
- Open source under AGPL-3.0: no secrets in the repository, only AGPL-compatible dependencies, no AAX code.

## Where the rules live
- Shared state between plugin instances, threading, the audio thread, offline bounces: DEVELOPMENT.md, "Traps & guidelines".
- Parameters (append-only, pinned by `tests/data/parameters.txt`; `tests/data/state-v1.bin` is never regenerated) and saved state (`Source/stage/StateRules.h`): DEVELOPMENT.md, "Architecture".
- The bridge between the page and C++ (`Source/bridge/PageProtocol.h` and `ui/src/bridge.ts` change together; every write is a gesture): DEVELOPMENT.md, "How the parts work".
- The head-tracking file's byte layout and timing are built into shipped plugins and never change: `tracker/README.md`.
- Usage data is opt-in and held to `PRIVACY.md` by `tests/TelemetryTests.cpp`: change the document, the code, the test and the popup's list (`ui/src/TelemetryConsent.tsx`) together.
- The visual design (tokens only, no hardcoded colours, the amber accent reserved for the live pulse): DEVELOPMENT.md, "Visual design".

## Documents
When behaviour changes, the document that describes it changes in the same commit: `README.md` for users, `docs/DEVELOPMENT.md` for the code, `PRIVACY.md` for anything sent anywhere, `THIRD_PARTY_NOTICES.md` and `scripts/third-party-licences.sh` for dependencies.
Dated reasons and measurements go to `docs/HISTORY.md`, not into the guide.
In Markdown, one sentence per line.
