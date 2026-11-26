# Privacy

Composer works offline and sends nothing anywhere, unless you agree to share anonymous usage data.

## The question

The first time a Composer window opens on a computer, it asks once whether you want to share anonymous usage data.
Nothing is collected, stored or sent before you say yes.
"No thanks" is a complete answer: Composer works exactly the same.
You can change the answer at any time in the settings (the sliders icon, "Share usage data").

The answer belongs to you as a user of this computer, not to a project: it is the same in every host and for every project.
It is kept in one small file:

- macOS: `~/Library/Application Support/Agogica/Composer.settings`
- Windows: `%LOCALAPPDATA%\Agogica\Composer.settings`

## What is sent after a yes

| When | What |
|---|---|
| A Composer window is opened for the first time since the host started | Composer's version, the plugin format (VST3, AU), the host application's name, the operating system and its version, the processor architecture |
| The next time, about the session before | Its length in minutes, the most tracks with Composer at once, whether Stereo, Binaural or both were used, the room size, how many scenes hold a layout, whether head tracking ran, the most tracks in one player |
| With that | The sample rate, the buffer size, Composer's share of one processor core (average and worst block), the number of blocks that took too long |
| If the window ever stops drawing | The error's message and the place in Composer's own page code where it happened, at most five per session |

Every event carries a random id that was made on your computer when you said yes.
It is not derived from anything about you or your machine.

## What is never sent

Names of tracks, players or projects, positions, sound, file names or paths, anything from the camera or Agogica Tracker, and your IP address is not stored (see below).

## Where it goes

To a PostHog project hosted in the European Union (`eu.i.posthog.com`), over HTTPS, by the plugin itself.
The project is set to discard IP addresses, and no profile of a person is created from the events.
The data is used only to see which hosts and systems Composer runs in, how it performs there and where it fails.

## Turning it off, and deleting

Turning sharing off deletes the random id and anything not yet sent from your computer, at once, for every host.
The id is shown under "About & privacy" in the settings while sharing is on.
To have what was already sent under your id deleted, send that id to Agogica (contact: see the README).

## Builds that never ask

Only official releases carry the key of the project the data goes to.
A build from source, a fork's build and every development build never asks and never sends.
Setting the environment variable `DO_NOT_TRACK=1` for the host has the same effect on any build.

## The code

Everything described here is in `Source/telemetry/` (what is sent, when, and the rules), `ui/src/TelemetryConsent.tsx` (the question) and `tests/TelemetryTests.cpp` (the tests that hold it to this page).
