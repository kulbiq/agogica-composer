# Contributing to Composer

Thank you for wanting to help.
Composer is free software under the GNU Affero General Public License, version 3, and contributions are accepted under the same licence.

## Before you start

- For anything larger than a fix, open an issue first and say what you would like to change.
  Decisions about the sound model, formats and what the plugin shares with anyone are the maintainers'.
- Building, the architecture and the rules that are easy to break are in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
  Read its "Traps & guidelines" before touching anything shared between plugin instances, threading or the bridge between the page and C++.

## Build and test

```sh
git clone --recurse-submodules https://github.com/kulbiq/agogica-composer.git
cd agogica-composer
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -LE slow        # the fast C++ tests
(cd ui && npm test)                  # the page's tests
```

Before a pull request, also run what touches your change:
- `ctest --preset debug` (everything, the slow cases included);
- `cmake --preset asan && cmake --build --preset asan && ctest --preset asan` (and `tsan`) for C++ changes;
- `scripts/pluginval.sh` on macOS (auval and pluginval at strictness 10) for anything in the plugin;
- `build/release/ComposerTests "[perf]" -s` (a Release build) for anything on the audio thread.

## Rules

- **A bug fix starts with a test that fails**, or a reproduction as a user would meet it, so the fix is known to fix the real problem.
- **Tests are a pyramid**: many fast unit tests of pure logic and single DSP parts, fewer `[integration]` tests (anything that builds a processor, or uses files, threads or an editor), very few `[slow]` ones.
  Add a check to an existing case when it covers the same behaviour.
- **The audio thread** never allocates, locks, logs or does I/O.
- **Parameters are forever**: hosts save projects and automation by parameter id, range and default.
  New parameters go at the end of `params::createLayout` and of `tests/data/parameters.txt`; existing ones never change, and `tests/data/state-v1.bin` is never regenerated.
- **The page and C++ change together**: the names of the bridge live once per side, in `Source/bridge/PageProtocol.h` and `ui/src/bridge.ts`.
  Every write from the page is a gesture (begin, change, end), or hosts do not record automation.
- **Dependencies** must be compatible with AGPL-3.0 (MIT, BSD, Apache, (L)GPL), and are listed in `THIRD_PARTY_NOTICES.md` and `scripts/third-party-licences.sh`.
  No AAX code: the Avid SDK is under NDA.
- **No secrets** in the repository: no certificates, credentials or keys.
- **Privacy**: anything the plugin sends anywhere must be described in `PRIVACY.md`, be opt-in, and be covered by `tests/TelemetryTests.cpp`.
- No compiler warnings, no failing or flaky tests: if you meet one, fix it, even if it is not yours.

## Commits and pull requests

- Small commits that each do one thing.
- The message starts with the area, a colon and a sentence that says what is true now, as in the log:
  `State: every read of the state tree holds its lock`, `Page: GL resources are given back`.
- Keep pull requests reviewable; say how you tested the change.
- `CHANGELOG` files and anything generated are never edited by hand.

## Sign your work (DCO)

Every commit must carry a `Signed-off-by` line with your name and the email address of the commit's author:

```sh
git commit -s -m "Area: what is true now"
```

By signing off you certify the [Developer Certificate of Origin 1.1](https://developercertificate.org/): that you wrote the change or otherwise have the right to submit it under the project's licence.
A check on every pull request refuses commits without it.

## Conduct and security

Be kind: see [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
Security problems are reported privately: see [SECURITY.md](SECURITY.md).
