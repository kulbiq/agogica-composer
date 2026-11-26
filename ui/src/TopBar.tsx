import { useState } from "react";
import styles from "./App.module.css";
import { setHeadTracking, setParams, setScene, startTracker, type Stage } from "./bridge";
import type { MapNames } from "./stagePainter";
import SettingsPopover from "./SettingsPopover";

const scenes = ["A", "B", "C", "D", "E", "F", "G", "H"];
const roomStep = 5; // roomSize's step and range (Source/Parameters.h)
const roomMin = 5;
const roomMax = 50;

type Props = { stage: Stage; selfId: number; names: MapNames; onNames: (names: MapNames) => void };

// Everything that applies to the whole stage, in one line: output, scene, room size, settings.
export default function TopBar({ stage, selfId, names, onNames }: Props) {
  const [settingsOpen, setSettingsOpen] = useState(false);

  return (
    <header className={styles.topBar}>
      <div className={styles.group}>
        <span className={styles.label}>Output</span>
        <div className={styles.segments} role="radiogroup" aria-label="Output">
          {(["stereo", "binaural"] as const).map((mode, i) => (
            <button
              key={mode}
              className={`${styles.chip} ${styles.segment}`}
              role="radio"
              aria-checked={stage.outputMode === mode}
              onClick={() => setParams(selfId, { outputMode: i })}
            >
              {mode === "stereo" ? "Stereo" : "Binaural"}
            </button>
          ))}
        </div>
        {stage.outputMode === "binaural" && <HeadTracking {...stage.headTracking} />}
      </div>

      <span className={styles.divider} />

      <div className={styles.group}>
        <span className={styles.label}>Scene</span>
        <div className={styles.segments} role="radiogroup" aria-label="Scene">
          {scenes.map((s) => (
            <button
              key={s}
              className={styles.chip}
              role="radio"
              aria-checked={stage.scene === s}
              data-empty={!stage.scenes.includes(s) || undefined}
              title={stage.scenes.includes(s) ? `Scene ${s}` : `Scene ${s} - starts as a copy of the current layout`}
              onClick={() => void setScene(s)}
            >
              {s}
            </button>
          ))}
        </div>
      </div>

      <span className={styles.divider} />

      <div className={styles.group}>
        <span className={styles.label} id="room-label">
          Room
        </span>
        <div className={styles.stepper} role="group" aria-labelledby="room-label">
          <button
            className={styles.step}
            aria-label="Smaller room"
            disabled={stage.room <= roomMin}
            onClick={() => setParams(selfId, { roomSize: Math.max(roomMin, stage.room - roomStep) })}
          >
            <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden>
              <path d="M2 5H8" />
            </svg>
          </button>
          <span className={styles.stepValue} aria-live="polite">
            {stage.room} m
          </span>
          <button
            className={styles.step}
            aria-label="Larger room"
            disabled={stage.room >= roomMax}
            onClick={() => setParams(selfId, { roomSize: Math.min(roomMax, stage.room + roomStep) })}
          >
            <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden>
              <path d="M2 5H8M5 2V8" />
            </svg>
          </button>
        </div>
      </div>

      <div className={styles.settingsAnchor}>
        <button
          className={styles.iconButton}
          aria-label="Settings"
          aria-expanded={settingsOpen}
          aria-haspopup="dialog"
          onClick={() => setSettingsOpen(!settingsOpen)}
        >
          <svg width="16" height="16" viewBox="0 0 16 16" aria-hidden>
            <path d="M2 4H14M2 8H14M2 12H14" />
            <circle cx="5" cy="4" r="1.7" />
            <circle cx="10.5" cy="8" r="1.7" />
            <circle cx="6.5" cy="12" r="1.7" />
          </svg>
        </button>
        {settingsOpen && (
          <SettingsPopover stage={stage} selfId={selfId} names={names} onNames={onNames} onClose={() => setSettingsOpen(false)} />
        )}
      </div>
    </header>
  );
}

// Head tracking: while Agogica Tracker runs, a switch (does the head turn the sound and the 3D
// view?); while it doesn't, the same chip starts it.
function HeadTracking({ live, canStart, on }: { live: boolean; canStart: boolean; on: boolean }) {
  const [failed, setFailed] = useState(false);

  if (live)
    return (
      <button
        className={styles.status}
        data-status={on ? "live" : "soon"}
        aria-pressed={on}
        title={on ? "Your head turns the sound. Click to stop following it" : "Head tracking is off. Click to follow your head"}
        onClick={() => void setHeadTracking(!on)}
      >
        <span className={styles.statusText}>{on ? "Tracking on" : "Tracking off"}</span>
      </button>
    );

  // Windows: Composer can't start the tracker yet (start it by hand).
  if (!canStart)
    return (
      <span className={styles.status} data-status="soon" title="Head tracking is off">
        <span className={styles.statusText}>Tracking off</span>
      </span>
    );

  return (
    <button
      className={styles.status}
      data-status="soon"
      title={failed ? "Agogica Tracker isn't installed (~/Applications)" : "Start Agogica Tracker"}
      onClick={() =>
        void startTracker().then((ok) => {
          setFailed(!ok);
          if (ok && !on) void setHeadTracking(true); // starting it means following the head
        })
      }
    >
      <span className={styles.statusText}>{failed ? "Tracker not found" : "Start tracking"}</span>
    </button>
  );
}
