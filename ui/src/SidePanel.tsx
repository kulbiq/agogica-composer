import { useRef, useState, type KeyboardEvent } from "react";
import { beginGesture, setName, setScene, setParams, type Gesture, type Param, type Stage } from "./juce";
import type { NodeKey } from "./StageMap";
import styles from "./App.module.css";

type Props = {
  stage: Stage;
  selfId: number;
  selected: NodeKey | null;
  onSelect: (key: NodeKey) => void;
};

const scenes = ["A", "B", "C", "D", "E", "F", "G", "H"];

const gestureKeys = new Set(["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "PageUp", "PageDown", "Home", "End"]);

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
        <RoomSize room={stage.room} selfId={selfId} />
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
              <dt className={styles.label}>Pan</dt>
              <dd>{formatPan(player.derived.pan)}</dd>
              <dt className={styles.label}>Delay</dt>
              <dd>{player.derived.delayMs.toFixed(1)} ms</dd>
            </dl>
          )}
        </section>
      )}

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
              </button>
            </li>
          ))}
        </ul>
      </section>
    </aside>
  );
}

function RoomSize({ room, selfId }: { room: number; selfId: number }) {
  // While dragging, show the local value; C++ confirms it a frame later.
  const [dragging, setDragging] = useState<number>();
  const gesture = useRef<Gesture>(undefined);
  const value = dragging ?? room;

  const begin = () => {
    gesture.current ??= beginGesture(selfId, ["roomSize"] satisfies Param[]);
  };
  const end = () => {
    gesture.current?.end();
    gesture.current = undefined;
    setDragging(undefined);
  };

  return (
    <label className={styles.field}>
      <span className={styles.row}>
        <span className={styles.label}>Room size</span>
        <span className={styles.value}>{value} m</span>
      </span>
      <input
        className={styles.range}
        type="range"
        min={5}
        max={50}
        step={5}
        value={value}
        onPointerDown={begin}
        onPointerUp={end}
        onPointerCancel={end}
        onKeyDown={(e) => gestureKeys.has(e.key) && begin()}
        onKeyUp={(e) => gestureKeys.has(e.key) && end()}
        onBlur={end}
        onChange={(e) => {
          begin();
          const v = Number(e.target.value);
          setDragging(v);
          gesture.current!.change({ roomSize: v });
        }}
      />
    </label>
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
