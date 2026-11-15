import { useRef, useState, type KeyboardEvent } from "react";
import { setName, setParams, setScene, startTracker, type Stage } from "./juce";
import ParamSlider from "./ParamSlider";
import ProPanel from "./ProPanel";
import type { NodeKey } from "./StageMap";
import styles from "./App.module.css";
import LevelMeter from "./LevelMeter";

type Props = {
  stage: Stage;
  selfId: number;
  selected: NodeKey | null;
  onSelect: (key: NodeKey) => void;
};

const scenes = ["A", "B", "C", "D", "E", "F", "G", "H"];

// "−15.6 dB" with a real minus sign; 0 shows as "0.0 dB".
const formatDb = (value: number) => `${value < -0.05 ? "\u2212" : ""}${Math.abs(value).toFixed(1)} dB`;

const formatPan = (pan: number) =>
  Math.abs(pan) < 0.005 ? "C" : `${pan < 0 ? "L" : "R"} ${Math.round(Math.abs(pan) * 100)}`;

export default function SidePanel({ stage, selfId, selected, onSelect }: Props) {
  const player = typeof selected === "number" ? stage.players.find((p) => p.id === selected) : undefined;
  const node = selected === "listener" ? stage.listener : player;

  return (
    <aside className={styles.panel}>
      <section className={styles.section}>
        <div className={styles.row}>
          <span className={styles.label}>Scene</span>
          {stage.alone && (
            <span className={styles.status} data-status="soon">
              Only this track
            </span>
          )}
        </div>
        <div className={styles.chips} role="radiogroup" aria-label="Scene">
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
      </section>

      <section className={styles.section}>
        <span className={styles.label}>Output</span>
        <div className={styles.segments} role="radiogroup" aria-label="Output">
          {(["stereo", "binaural"] as const).map((mode, i) => (
            <button
              key={mode}
              className={styles.chip}
              role="radio"
              aria-checked={stage.outputMode === mode}
              onClick={() => setParams(selfId, { outputMode: i })}
            >
              {mode === "stereo" ? "Stereo" : "Binaural"}
            </button>
          ))}
        </div>
        {stage.outputMode === "binaural" ? (
          <HeadTracking {...stage.headTracking} />
        ) : (
          <p className={styles.note}>For speakers. Players behind the listener are mirrored to the front.</p>
        )}
      </section>

      <section className={styles.section}>
        <ParamSlider
          param="roomSize"
          label="Room size"
          value={stage.room}
          min={5}
          max={50}
          step={5}
          format={(v) => `${v} m`}
          selfId={selfId}
        />
      </section>

      {node && (
        <section className={styles.section}>
          {player ? (
            <NameField key={player.id} id={player.id} name={player.name} />
          ) : (
            <span className={styles.nodeTitle}>Listener</span>
          )}
          <div className={styles.fields}>
            {(["x", "y"] as const).map((axis) => (
              <NumberField
                key={`${String(selected)}-${axis}`}
                label={axis}
                value={node[axis]}
                room={stage.room}
                onCommit={(v) =>
                  player
                    ? setParams(player.id, { [axis === "x" ? "playerX" : "playerY"]: v })
                    : setParams(selfId, { [axis === "x" ? "listenerX" : "listenerY"]: v })
                }
              />
            ))}
          </div>
          {player && (
            <dl className={styles.derived}>
              <dt className={styles.label}>Distance</dt>
              <dd>{player.derived.distance.toFixed(2)} m</dd>
              <dt className={styles.label}>Azimuth</dt>
              <dd>{player.derived.azimuth.toFixed(1)}°</dd>
              {stage.outputMode === "stereo" && (
                <>
                  <dt className={styles.label}>Pan</dt>
                  <dd>{formatPan(player.derived.pan)}</dd>
                </>
              )}
              <dt className={styles.label}>Level</dt>
              <dd>{formatDb(player.derived.levelDb)}</dd>
              <dt className={styles.label} title="High-frequency loss in air, at 8 kHz">
                Air · 8 kHz
              </dt>
              <dd>{formatDb(player.derived.airDb)}</dd>
            </dl>
          )}
        </section>
      )}

      <section className={styles.section}>
        <ProPanel values={stage.pro} selfId={selfId} />
      </section>

      <section className={styles.section}>
        <span className={styles.label}>Players</span>
        <ul className={styles.list}>
          <li>
            <button className={styles.item} aria-pressed={selected === "listener"} onClick={() => onSelect("listener")}>
              <span className={styles.tick} />
              Listener
            </button>
          </li>
          {stage.players.map((p) => (
            <li key={p.id}>
              <button className={styles.item} aria-pressed={selected === p.id} onClick={() => onSelect(p.id)}>
                <span className={styles.tick} style={p.colour ? { background: p.colour } : undefined} />
                {p.name}
                {p.self && <span className={styles.you}>This track</span>}
                <LevelMeter id={p.id} className={styles.listMeter} />
              </button>
            </li>
          ))}
        </ul>
      </section>
    </aside>
  );
}


// Head tracking status; while it isn't running, the same chip starts Agogica Tracker.
function HeadTracking({ live, canStart }: { live: boolean; canStart: boolean }) {
  const [failed, setFailed] = useState(false);

  if (live)
    return (
      <span className={styles.status} data-status="live">
        Head tracking · live
      </span>
    );

  // Windows: Composer can't start the tracker yet (start it by hand).
  if (!canStart)
    return (
      <span className={styles.status} data-status="soon">
        Head tracking · off
      </span>
    );

  return (
    <button
      className={styles.status}
      data-status="soon"
      title={failed ? "Agogica Tracker isn't installed (~/Applications)" : "Start Agogica Tracker"}
      onClick={() => void startTracker().then((ok) => setFailed(!ok))}
    >
      {failed ? "Tracker not found" : "Start head tracking"}
    </button>
  );
}

function NumberField({
  label,
  value,
  room,
  onCommit,
}: {
  label: string;
  value: number;
  room: number;
  onCommit: (v: number) => void;
}) {
  const [draft, setDraft] = useState<string>();
  const cancelled = useRef(false);

  const commit = () => {
    const v = Number(draft);
    if (!cancelled.current && draft !== undefined && draft.trim() !== "" && Number.isFinite(v)) {
      const half = room / 2;
      onCommit(Math.round(Math.min(half, Math.max(-half, v)) * 100) / 100);
    }
    cancelled.current = false;
    setDraft(undefined);
  };

  const onKeyDown = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.key === "Escape") cancelled.current = true;
    if (e.key === "Enter" || e.key === "Escape") e.currentTarget.blur();
  };

  return (
    <label className={styles.numberField}>
      <span className={styles.label}>{label}</span>
      <input
        className={styles.input}
        type="text"
        inputMode="decimal"
        value={draft ?? value.toFixed(2)}
        onFocus={(e) => {
          setDraft(value.toFixed(2));
          e.currentTarget.select();
        }}
        onChange={(e) => setDraft(e.target.value)}
        onBlur={commit}
        onKeyDown={onKeyDown}
      />
      <span className={styles.unit}>m</span>
    </label>
  );
}

function NameField({ id, name }: { id: number; name: string }) {
  const [draft, setDraft] = useState<string>();
  const cancelled = useRef(false);

  const commit = () => {
    if (!cancelled.current && draft !== undefined && draft.trim() !== name) void setName(id, draft.trim());
    cancelled.current = false;
    setDraft(undefined);
  };

  return (
    <input
      className={styles.nameInput}
      aria-label="Player name"
      value={draft ?? name}
      onFocus={() => setDraft(name)}
      onChange={(e) => setDraft(e.target.value)}
      onBlur={commit}
      onKeyDown={(e) => {
        if (e.key === "Escape") cancelled.current = true;
        if (e.key === "Enter" || e.key === "Escape") e.currentTarget.blur();
      }}
    />
  );
}
