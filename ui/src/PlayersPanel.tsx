import { useRef, useState } from "react";
import styles from "./App.module.css";
import { setName, type Stage } from "./bridge";
import EditPlayer from "./EditPlayer";
import { Icon, playerIcon } from "./icons";
import LevelMeter from "./LevelMeter";
import type { NodeKey } from "./stagePainter";

type Props = {
  stage: Stage;
  selected: NodeKey | null;
  onSelect: (key: NodeKey) => void;
};

// The players, listener first; the selected row is the one the map highlights.
// Double-click (or F2) a name to rename the player; an empty name goes back to the track's.
// "Edit player" under the selected row drills the panel into that player.
export default function PlayersPanel({ stage, selected, onSelect }: Props) {
  const [renaming, setRenaming] = useState<number>();
  const [editing, setEditing] = useState<number>();
  const edited = stage.players.find((p) => p.id === editing); // gone if its track was removed

  return (
    <aside className={styles.players}>
      {edited && <EditPlayer key={edited.id} player={edited} onBack={() => setEditing(undefined)} />}
      <div className={styles.playersHead}>
        <span className={styles.label}>Players · {stage.players.length}</span>
        {stage.alone && (
          <span className={styles.status} data-status="soon">
            Only this track
          </span>
        )}
      </div>
      <ul className={styles.list}>
        <li>
          <button className={styles.item} aria-pressed={selected === "listener"} onClick={() => onSelect("listener")}>
            <span className={styles.tick} />
            <svg className={styles.itemIcon} width="16" height="16" viewBox="-8 -8 16 16" aria-hidden>
              <path d="M0 -6L4.1 4.5L0 2.2L-4.1 4.5Z" fill="currentColor" />
            </svg>
            <span className={styles.itemName}>Listener</span>
          </button>
        </li>
        {stage.players.map((p) => (
          <li key={p.id}>
            {renaming === p.id ? (
              <div className={styles.item} data-editing>
                <span className={styles.tick} style={p.colour ? { background: p.colour } : undefined} />
                <Icon icon={playerIcon(p)} className={styles.itemIcon} />
                <NameField id={p.id} name={p.name} onDone={() => setRenaming(undefined)} />
              </div>
            ) : (
              <button
                className={styles.item}
                aria-pressed={selected === p.id}
                title="Double-click to rename"
                onClick={() => onSelect(p.id)}
                onDoubleClick={() => setRenaming(p.id)}
                onKeyDown={(e) => e.key === "F2" && setRenaming(p.id)}
              >
                <span className={styles.tick} style={p.colour ? { background: p.colour } : undefined} />
                <Icon icon={playerIcon(p)} className={styles.itemIcon} />
                <span className={styles.itemName}>{p.name}</span>
                {p.self && <span className={styles.you}>This track</span>}
                <LevelMeter id={p.id} className={styles.listMeter} />
              </button>
            )}
            {selected === p.id && renaming !== p.id && (
              <div className={styles.selBar}>
                <button className={styles.mini} onClick={() => setEditing(p.id)}>
                  Edit player
                </button>
              </div>
            )}
          </li>
        ))}
      </ul>
    </aside>
  );
}

function NameField({ id, name, onDone }: { id: number; name: string; onDone: () => void }) {
  const [draft, setDraft] = useState(name);
  const cancelled = useRef(false);

  const commit = () => {
    if (!cancelled.current && draft.trim() !== name) void setName(id, draft.trim());
    onDone();
  };

  return (
    <input
      className={styles.nameInput}
      aria-label="Player name"
      autoFocus
      value={draft}
      onFocus={(e) => e.currentTarget.select()}
      onChange={(e) => setDraft(e.target.value)}
      onBlur={commit}
      onKeyDown={(e) => {
        if (e.key === "Escape") cancelled.current = true;
        if (e.key === "Enter" || e.key === "Escape") e.currentTarget.blur();
      }}
    />
  );
}
