import { useEffect, useRef, useState } from "react";
import styles from "./App.module.css";
import { setIcon, setName, setTrackName } from "./bridge";
import { Icon, iconKeys, iconLabels, playerIcon, type IconKey } from "./icons";
import type { Seat } from "./players";

type Props = {
  player: Seat;
  others: Seat[]; // the other players, whose tracks can be added to this one
  onSplit: (trackId: number) => void;
  onAdd: (trackIds: number[]) => void;
  onBack: () => void;
};

// One player's name, icon and tracks, in a sheet that opens from the players panel over the
// map. The icon and tracks apply at once; the name on Enter, Done, Back, Escape or a press
// outside. An empty name goes back to the track's.
export default function EditPlayer({ player, others, onSplit, onAdd, onBack }: Props) {
  const [draft, setDraft] = useState(player.name);
  const [adding, setAdding] = useState(false);
  const root = useRef<HTMLDivElement>(null);

  const commitName = () => {
    if (draft.trim() !== player.name) void setName(player.id, draft.trim());
  };
  const close = () => {
    commitName();
    onBack();
  };

  const latestClose = useRef(close);
  latestClose.current = close;
  useEffect(() => {
    const onPointerDown = (e: PointerEvent) => !root.current?.contains(e.target as Node) && latestClose.current();
    const onKeyDown = (e: KeyboardEvent) => e.key === "Escape" && latestClose.current();
    document.addEventListener("pointerdown", onPointerDown);
    document.addEventListener("keydown", onKeyDown);
    return () => {
      document.removeEventListener("pointerdown", onPointerDown);
      document.removeEventListener("keydown", onKeyDown);
    };
  }, []);

  const cell = (key: IconKey | "", icon: IconKey, label: string, title: string) => (
    <button
      key={key}
      className={styles.iconCell}
      role="radio"
      aria-checked={player.icon === key}
      aria-label={title}
      title={title}
      onClick={() => void setIcon(player.id, key)}
    >
      <Icon icon={icon} size={28} />
      <span>{label}</span>
    </button>
  );

  return (
    <div className={styles.sheet} ref={root} role="dialog" aria-label="Edit player">
      <div className={styles.sheetHead}>
        <button className={styles.back} aria-label="Back to players" onClick={close}>
          <svg width="14" height="14" viewBox="0 0 10 10" aria-hidden>
            <path d="M6.5 2L3.5 5L6.5 8" />
          </svg>
        </button>
        <span className={`${styles.label} ${styles.strong}`}>Edit player</span>
        {player.self && <span className={styles.you}>This track</span>}
      </div>

      <div className={styles.sheetBody}>
        <label className={styles.field}>
          <span className={styles.label}>Name</span>
          <input
            className={styles.textInput}
            value={draft}
            maxLength={64}
            autoFocus
            onFocus={(e) => e.currentTarget.select()}
            onChange={(e) => setDraft(e.target.value)}
            onBlur={commitName}
            onKeyDown={(e) => e.key === "Enter" && e.currentTarget.blur()}
          />
        </label>

        <div className={styles.field}>
          <span className={styles.label} id="icon-label">
            Icon
          </span>
          <div className={styles.iconGrid} role="radiogroup" aria-labelledby="icon-label">
            {cell("", playerIcon({ ...player, icon: "" }), "Auto", `Automatic: ${iconLabels[playerIcon({ ...player, icon: "" })]}`)}
            {iconKeys.map((key) => cell(key, key, iconLabels[key].split(",")[0], iconLabels[key]))}
          </div>
          <p className={styles.note}>Auto picks the icon from the player's or its tracks' names (pno, vln, hn…).</p>
        </div>

        <div className={styles.field}>
          <span className={styles.label}>Tracks · {player.tracks.length}</span>
          <ul className={styles.editTracks}>
            {player.tracks.map((t) => (
              <li key={t.id} className={styles.editTrack}>
                <span className={styles.tick} style={t.colour ? { background: t.colour } : undefined} />
                <TrackName id={t.id} name={t.track} />
                {t.self && <span className={styles.you}>This track</span>}
                {player.tracks.length > 1 && (
                  <button className={styles.mini} onClick={() => onSplit(t.id)}>
                    Split off
                  </button>
                )}
              </li>
            ))}
          </ul>
          {others.length > 0 && (
            <button className={styles.dashed} aria-expanded={adding} onClick={() => setAdding(!adding)}>
              + Add a track
            </button>
          )}
          {adding && (
            <ul className={styles.addList} aria-label="Players whose tracks join this one">
              {others.map((s) => (
                <li key={s.id}>
                  <button
                    className={styles.item}
                    onClick={() => {
                      onAdd(s.tracks.map((t) => t.id));
                      setAdding(false);
                    }}
                  >
                    <span className={styles.tick} style={s.colour ? { background: s.colour } : undefined} />
                    <Icon icon={playerIcon(s)} className={styles.itemIcon} />
                    <span className={styles.itemName}>{s.name}</span>
                    {s.tracks.length > 1 && <span className={styles.count}>{s.tracks.length}</span>}
                  </button>
                </li>
              ))}
            </ul>
          )}
          <p className={styles.note}>A player's tracks stand in one place and move together. Each keeps its own name and level.</p>
        </div>
      </div>

      <div className={styles.sheetFoot}>
        <button className={styles.primary} onClick={close}>
          Done
        </button>
      </div>
    </div>
  );
}

// A track's own name, edited in place (the player's name is the field above).
function TrackName({ id, name }: { id: number; name: string }) {
  const [draft, setDraft] = useState<string>(); // while the field has focus
  const commit = () => {
    if (draft !== undefined && draft.trim() !== name) void setTrackName(id, draft.trim());
    setDraft(undefined);
  };

  return (
    <input
      className={styles.trackInput}
      aria-label="Track name"
      value={draft ?? name}
      maxLength={64}
      onFocus={() => setDraft(name)}
      onChange={(e) => setDraft(e.target.value)}
      onBlur={commit}
      onKeyDown={(e) => e.key === "Enter" && e.currentTarget.blur()}
    />
  );
}
