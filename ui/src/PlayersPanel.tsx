import { useRef, useState, type PointerEvent as ReactPointerEvent, type ReactNode } from "react";
import styles from "./App.module.css";
import { joinPlayer, setName, setTrackName, splitPlayer } from "./bridge";
import EditPlayer from "./EditPlayer";
import { Icon, playerIcon } from "./icons";
import LevelMeter, { MeterLayer } from "./LevelMeter";
import type { Seat, SeatedStage } from "./players";
import { toggle } from "./mapEdits";
import type { NodeKey } from "./stagePainter";

type Props = {
  stage: SeatedStage;
  selected: NodeKey | null; // the one that leads
  selection: readonly NodeKey[]; // all selected, the leading one last
  onSelect: (keys: NodeKey[]) => void;
};

// What is being dragged: a whole player (all its tracks) or one of its tracks.
type Drag = { tracks: number[]; from: number; label: string; x: number; y: number; over?: number | "out" };

// Moves the given tracks into player `into` (one after another: each join reads the stage).
const joinAll = async (tracks: number[], into: number) => {
  for (const t of tracks) await joinPlayer(t, into);
};

// The players, listener first; the selected row is the one the map highlights.
// - Double-click (or F2) a name to rename the player; an empty name goes back to the track's.
// - "Edit player" under the selected row opens its name, icon and tracks.
// - A player with several tracks shows how many; the arrow lists them.
// - Drag a player (or one of its tracks) onto another to make them one player; drag a track
//   out of its player to make it a player of its own.
// - Shift, Cmd or Ctrl and a click selects several (on the map too); with two players or more
//   selected they can be merged. "Select" does the same with plain clicks.
export default function PlayersPanel({ stage, selected, selection, onSelect }: Props) {
  const [renaming, setRenaming] = useState<number>();
  const [editing, setEditing] = useState<number>(); // a track of the player being edited
  const [expanded, setExpanded] = useState<ReadonlySet<number>>(new Set());
  const [picking, setPicking] = useState(false); // plain clicks add to the selection, to merge
  const chosen = selection.filter((k): k is number => k !== "listener"); // the selected players
  const [drag, setDrag] = useState<Drag>();
  const dragged = useRef(false); // a drag just ended: the click that follows is not a click

  const seats = stage.players;
  const edited = seats.find((s) => s.tracks.some((t) => t.id === editing)); // gone if its tracks were removed
  const trackCount = seats.reduce((n, s) => n + s.tracks.length, 0);

  const startDrag = (e: ReactPointerEvent, source: Omit<Drag, "x" | "y">) => {
    if (e.button !== 0 || picking) return;
    const start = { x: e.clientX, y: e.clientY };
    const element = e.currentTarget as HTMLElement;
    const ownSeat = seats.find((s) => s.id === source.from)!;
    const leavesPlayer = source.tracks.length < ownSeat.tracks.length; // one track of several
    let current: Drag | undefined;

    const move = (ev: PointerEvent) => {
      if (!current && Math.hypot(ev.clientX - start.x, ev.clientY - start.y) < 5) return;
      const under = document.elementFromPoint(ev.clientX, ev.clientY);
      const seatId = Number(under?.closest<HTMLElement>("[data-seat]")?.dataset.seat ?? NaN);
      const outside = !under?.closest(`[data-seat="${source.from}"]`) && !!under?.closest("aside");
      const over = !Number.isNaN(seatId) && seatId !== source.from ? seatId : leavesPlayer && outside ? "out" : undefined;
      current = { ...source, x: ev.clientX, y: ev.clientY, over };
      setDrag(current);
    };
    const up = () => {
      element.removeEventListener("pointermove", move);
      element.removeEventListener("pointerup", up);
      element.removeEventListener("pointercancel", up);
      if (!current) return;
      dragged.current = true;
      setTimeout(() => (dragged.current = false));
      if (typeof current.over === "number") {
        void joinAll(current.tracks, current.over);
        onSelect([current.over]);
      } else if (current.over === "out") void splitPlayer(current.tracks[0]);
      setDrag(undefined);
    };

    element.setPointerCapture(e.pointerId);
    element.addEventListener("pointermove", move);
    element.addEventListener("pointerup", up);
    element.addEventListener("pointercancel", up);
  };

  const merge = async (ids: number[]) => {
    const [into, ...rest] = seats.filter((s) => ids.includes(s.id)); // in list order
    for (const s of rest) await joinAll(s.tracks.map((t) => t.id), into.id);
    setPicking(false);
    onSelect([into.id]);
  };

  const tick = (colour: string) => <span className={styles.tick} style={colour ? { background: colour } : undefined} />;

  return (
    <aside className={styles.players} data-drop={drag?.over === "out" || undefined}>
      <MeterLayer />
      {edited && (
        <EditPlayer
          key={edited.id}
          player={edited}
          others={seats.filter((s) => s !== edited)}
          onSplit={(trackId) => {
            // Splitting off the track the sheet follows: follow one that stays.
            if (trackId === editing) setEditing(edited.tracks.find((t) => t.id !== trackId)?.id);
            void splitPlayer(trackId);
          }}
          onAdd={(tracks) => void joinAll(tracks, edited.id)}
          onBack={() => setEditing(undefined)}
        />
      )}

      <div className={styles.playersHead}>
        {picking ? (
          <span className={`${styles.label} ${styles.strong}`}>Select players</span>
        ) : (
          <span className={styles.label} title={`${trackCount} tracks`}>
            Players · {seats.length}
          </span>
        )}
        {stage.alone ? (
          <span className={styles.status} data-status="soon">
            Only this track
          </span>
        ) : (
          <button
            className={styles.mini}
            onClick={() => {
              if (!picking) onSelect(chosen); // the listener can't be merged
              setPicking(!picking);
            }}
          >
            {picking ? "Cancel" : "Select"}
          </button>
        )}
      </div>

      <ul className={styles.list}>
        {!picking && (
          <li>
            <button
              className={styles.item}
              aria-pressed={selection.includes("listener")}
              onClick={(e) => onSelect(e.shiftKey || e.metaKey || e.ctrlKey ? toggle(selection, "listener") : ["listener"])}
            >
              <span className={styles.tick} />
              <svg className={styles.itemIcon} width="16" height="16" viewBox="-8 -8 16 16" aria-hidden>
                <path d="M0 -6L4.1 4.5L0 2.2L-4.1 4.5Z" fill="currentColor" />
              </svg>
              <span className={styles.itemName}>Listener</span>
            </button>
          </li>
        )}

        {seats.map((s) => (
          <SeatRow
            key={s.id}
            seat={s}
            selected={selection.includes(s.id)}
            leads={selected === s.id && selection.length === 1} // several selected: the Merge bar instead
            picking={picking}
            renaming={renaming === s.id}
            open={expanded.has(s.id)}
            drag={drag}
            tick={tick}
            onSelect={(extend) => !dragged.current && onSelect(picking || extend ? toggle(selection, s.id) : [s.id])}
            onRename={(on) => setRenaming(on ? s.id : undefined)}
            onToggle={() => {
              const next = new Set(expanded);
              if (!next.delete(s.id)) next.add(s.id);
              setExpanded(next);
            }}
            onEdit={() => setEditing(s.id)}
            onDragStart={startDrag}
          />
        ))}
      </ul>

      {(picking || chosen.length > 1) && (
        <div className={styles.mergeBar}>
          <p className={styles.note}>
            {chosen.length < 2
              ? "Pick the players that play as one: their tracks will stand in one place."
              : `${chosen.length} players become one, with the first one's place, name and icon.`}
          </p>
          <button className={styles.primary} disabled={chosen.length < 2} onClick={() => void merge(chosen)}>
            Merge
          </button>
        </div>
      )}

      {drag && drag.over !== undefined && (
        <div className={styles.dragLabel} style={{ left: drag.x + 14, top: drag.y + 12 }}>
          {drag.over === "out" ? `${drag.label}: a player of its own` : `${drag.label} → ${seats.find((s) => s.id === drag.over)?.name}`}
        </div>
      )}
    </aside>
  );
}

type RowProps = {
  seat: Seat;
  selected: boolean;
  leads: boolean; // the selected one that "Edit player" opens
  picking: boolean;
  renaming: boolean;
  open: boolean;
  drag: Drag | undefined;
  tick: (colour: string) => ReactNode;
  onSelect: (extend: boolean) => void; // extend: add or take out, keeping the others
  onRename: (on: boolean) => void;
  onToggle: () => void;
  onEdit: () => void;
  onDragStart: (e: ReactPointerEvent, source: Omit<Drag, "x" | "y">) => void;
};

// One player: its row, the list of its tracks when it has several and is open, and the
// actions under it when selected.
function SeatRow({ seat: s, selected, leads, picking, renaming, open, drag, tick, onSelect, onRename, onToggle, onEdit, onDragStart }: RowProps) {
  const several = s.tracks.length > 1;
  const ids = s.tracks.map((t) => t.id);
  const [renamingTrack, setRenamingTrack] = useState<number>();
  const count = several && <span className={styles.count}>{s.tracks.length}</span>;

  return (
    <li
      className={styles.seat}
      data-seat={s.id}
      data-drop={drag?.over === s.id || undefined}
      data-dragged={(drag?.over !== undefined && drag.from === s.id && drag.tracks.length === s.tracks.length) || undefined}
    >
      {picking ? (
        <button className={styles.item} aria-pressed={selected} onClick={() => onSelect(true)}>
          <span className={styles.check} aria-hidden>
            <svg width="10" height="10" viewBox="0 0 10 10">
              <path d="M2 5.2L4.2 7.4L8 2.8" />
            </svg>
          </span>
          <Icon icon={playerIcon(s)} className={styles.itemIcon} />
          <span className={styles.itemName} title={s.name}>{s.name}</span>
          {count}
        </button>
      ) : renaming ? (
        <div className={styles.item} data-editing>
          {tick(s.colour)}
          <Icon icon={playerIcon(s)} className={styles.itemIcon} />
          <NameField name={s.name} label="Player name" onCommit={(n) => void setName(s.id, n)} onDone={() => onRename(false)} />
        </div>
      ) : (
        <button
          className={styles.item}
          aria-pressed={selected}
          title="Double-click to rename. Drag onto another player to join them."
          onClick={(e) => onSelect(e.shiftKey || e.metaKey || e.ctrlKey)}
          onDoubleClick={() => onRename(true)}
          onKeyDown={(e) => e.key === "F2" && onRename(true)}
          onPointerDown={(e) => onDragStart(e, { tracks: ids, from: s.id, label: s.name })}
        >
          {tick(s.colour)}
          <Icon icon={playerIcon(s)} className={styles.itemIcon} />
          <span className={styles.itemName} title={s.name}>{s.name}</span>
          {s.self && <span className={styles.you}>This track</span>}
          {count}
          <LevelMeter ids={ids} className={styles.listMeter} />
        </button>
      )}

      {several && !picking && !renaming && (
        <button
          className={styles.expand}
          aria-expanded={open}
          aria-label={`Tracks of ${s.name}`}
          onClick={onToggle}
        >
          <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden>
            <path d={open ? "M2 3.5L5 6.5L8 3.5" : "M3.5 2L6.5 5L3.5 8"} />
          </svg>
        </button>
      )}

      {several && open && !picking && (
        <ul className={styles.tracks}>
          {s.tracks.map((t, i) => (
            <li
              key={t.id}
              className={styles.trackRow}
              data-dragged={(drag?.over !== undefined && drag.tracks.length === 1 && drag.tracks[0] === t.id) || undefined}
              title="Double-click to rename the track. Drag it out of the player to make it a player of its own, or onto another player."
              onPointerDown={(e) => renamingTrack !== t.id && onDragStart(e, { tracks: [t.id], from: s.id, label: t.track })}
              onDoubleClick={() => setRenamingTrack(t.id)}
            >
              <svg className={styles.tree} width="12" height="28" viewBox="0 0 12 28" aria-hidden>
                <path d={i === s.tracks.length - 1 ? "M4 0V14H11" : "M4 0V28M4 14H11"} />
              </svg>
              {tick(t.colour)}
              {renamingTrack === t.id ? (
                <NameField name={t.track} label="Track name" onCommit={(n) => void setTrackName(t.id, n)} onDone={() => setRenamingTrack(undefined)} />
              ) : (
                <span className={styles.itemName} title={t.track}>{t.track}</span>
              )}
              {t.self && <span className={styles.you}>This track</span>}
              <LevelMeter ids={[t.id]} className={styles.listMeter} />
            </li>
          ))}
        </ul>
      )}

      {leads && !picking && !renaming && (
        <div className={styles.selBar}>
          <button className={styles.mini} onClick={onEdit}>
            Edit player
          </button>
        </div>
      )}
    </li>
  );
}

// Renames in place: Enter or leaving the field commits, Escape cancels.
function NameField({ name, label, onCommit, onDone }: { name: string; label: string; onCommit: (name: string) => void; onDone: () => void }) {
  const [draft, setDraft] = useState(name);
  const cancelled = useRef(false);

  const commit = () => {
    if (!cancelled.current && draft.trim() !== name) onCommit(draft.trim());
    onDone();
  };

  return (
    <input
      className={styles.nameInput}
      aria-label={label}
      maxLength={64} // what C++ keeps (Source/stage/StateRules.h)
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
