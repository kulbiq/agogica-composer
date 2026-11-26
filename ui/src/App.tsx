import { useCallback, useEffect, useMemo, useState, type CSSProperties, type KeyboardEvent, type PointerEvent } from "react";
import styles from "./App.module.css";
import { useStage } from "./bridge";
import { seat } from "./players";
import PlayersPanel from "./PlayersPanel";
import { useNumberPref, usePref } from "./prefs";
import ResizeGrip from "./ResizeGrip";
import StageMap, { type Brush, type MapNames, type MapView, type NodeKey } from "./StageMap";
import TopBar from "./TopBar";

const views = ["grid", "azimuth"] as const satisfies readonly MapView[];
const nameModes = ["selected", "all"] as const satisfies readonly MapNames[];
const walkModes = ["move", "spectate"] as const;

export default function App() {
  const snapshot = useStage();
  const stage = useMemo(() => snapshot && seat(snapshot), [snapshot]); // a player's tracks as one
  const selfId = stage?.players.find((p) => p.self)?.id;
  const [picks, setPicks] = useState<NodeKey[] | null>(null);
  const [view, setView] = usePref<MapView>("mapView", "grid", views);
  const [names, setNames] = usePref<MapNames>("mapNames", "selected", nameModes);
  const [in3d, setIn3d] = useState(false); // not saved: the editor always opens on the map
  const [brush, setBrush] = useState<Brush | null>(null); // painting the audience
  // W A S D and turning: "move" moves the listener (the sound follows), "spectate" only the view.
  const [walkMode, setWalkMode] = usePref("walkMode", "move", walkModes);
  const [zoomed, setZoomed] = useState(false);
  const [fitRequest, setFitRequest] = useState(0);

  // The map leaves room above the room for its tools, one row or, in a narrow window, two.
  const [toolsBottom, setToolsBottom] = useState(46);
  const tools = useCallback((el: HTMLDivElement | null) => {
    if (!el) return;
    const ro = new ResizeObserver(() => setToolsBottom(el.offsetTop + el.offsetHeight));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);
  const [panel, setPanel] = useNumberPref("panelWidth", panelWidth.initial, panelWidth.min, panelWidth.max);

  // Own player is selected until the user picks something. Picks are tracks (or the listener):
  // each stays selected as part of whichever player it joins; vanished ones drop out, and when
  // all have, own player is selected again. The last pick leads (Edit player, Q and E).
  const seatOf = (key: NodeKey) =>
    key === "listener" ? key : stage?.players.find((s) => s.tracks.some((t) => t.id === key))?.id;
  const found = (picks ?? []).map(seatOf).filter((k) => k !== undefined);
  const seats = found.filter((k, i) => found.lastIndexOf(k) === i); // two tracks of one player: once
  const selection = seats.length > 0 || picks?.length === 0 || selfId === undefined ? seats : [selfId];
  const selected = selection.at(-1) ?? null;

  // One Composer window is enough (each shows every track), but a plugin can't close host
  // windows. So a window opened while another is open steps aside until the user insists
  // or the other one closes; after that it stays open for good.
  const [blocked, setBlocked] = useState<boolean | null>(null);
  useEffect(() => {
    if (stage) setBlocked((b) => (b ?? true) && stage.openElsewhere !== undefined);
  }, [stage]);

  return (
    <div className={styles.root}>
      {stage && blocked && stage.openElsewhere !== undefined ? (
        <div className={`${styles.body} ${styles.elsewhere}`}>
          <span className={styles.label}>Composer is already open</span>
          <h1 className={styles.elsewhereTitle}>{stage.openElsewhere}</h1>
          <p className={styles.elsewhereText}>
            Every Composer window shows the whole stage. Close this one and continue in the window of{" "}
            {stage.openElsewhere}.
          </p>
          <button className={styles.ghost} onClick={() => setBlocked(false)}>
            Open here anyway
          </button>
        </div>
      ) : stage && selfId !== undefined && blocked === false ? (
        <>
          <TopBar stage={stage} selfId={selfId} names={names} onNames={setNames} />
          <div className={styles.body} style={{ "--panel": `${panel}px` } as CSSProperties}>
            <PlayersPanel stage={stage} selected={selected} selection={selection} onSelect={setPicks} />
            <PanelEdge width={panel} onWidth={setPanel} />
            <div className={styles.mapArea}>
              <StageMap
                stage={stage}
                selfId={selfId}
                selected={selected}
                selection={selection}
                onSelect={setPicks}
                view={view}
                names={names}
                in3d={in3d}
                onLeave3d={() => setIn3d(false)}
                brush={brush}
                spectate={walkMode === "spectate"}
                top={toolsBottom + 10} // the gap the map has always had under one row
                fitRequest={fitRequest}
                onZoomed={setZoomed}
              />
              <div className={`${styles.mapTools} ${in3d ? styles.mapToolsRoom : ""}`} ref={tools}>
                <div className={`${styles.segments} ${styles.viewSwitch}`} role="radiogroup" aria-label="View">
                  {views.map((v) => (
                    <button
                      key={v}
                      className={`${styles.chip} ${styles.segment}`}
                      role="radio"
                      aria-checked={!in3d && view === v}
                      onClick={() => {
                        setView(v);
                        setIn3d(false);
                      }}
                    >
                      {v === "grid" ? "Grid" : "Azimuth"}
                    </button>
                  ))}
                  <button
                    className={`${styles.chip} ${styles.segment}`}
                    role="radio"
                    aria-checked={in3d}
                    title="Walk the room at the listener's eyes"
                    onClick={() => {
                      setIn3d(true);
                      setBrush(null); // painting is for the map
                    }}
                  >
                    3D
                  </button>
                </div>
                <div className={`${styles.segments} ${styles.viewSwitch}`} role="radiogroup" aria-label="W A S D">
                  {walkModes.map((m) => (
                    <button
                      key={m}
                      className={`${styles.chip} ${styles.segment}`}
                      role="radio"
                      aria-checked={walkMode === m}
                      title={
                        m === "move"
                          ? "W A S D and turning move the listener: the sound follows"
                          : "W A S D and turning move only your view: the sound stays"
                      }
                      onClick={() => setWalkMode(m)}
                    >
                      {m === "move" ? "Move" : "Spectate"}
                    </button>
                  ))}
                </div>
                {!in3d && (
                  <div className={`${styles.segments} ${styles.viewSwitch}`}>
                    <button
                      className={`${styles.chip} ${styles.segment}`}
                      aria-pressed={brush !== null}
                      title={brush ? "Done painting the audience" : "Paint where the audience sits"}
                      onClick={() => setBrush(brush ? null : "paint")}
                    >
                      Audience
                    </button>
                  </div>
                )}
                {!in3d && zoomed && (
                  <div className={`${styles.segments} ${styles.viewSwitch}`}>
                    <button className={`${styles.chip} ${styles.segment}`} title="Show the whole room" onClick={() => setFitRequest((n) => n + 1)}>
                      Fit
                    </button>
                  </div>
                )}
              </div>
              {brush && !in3d && (
                <div className={`${styles.segments} ${styles.viewSwitch} ${styles.brushBar}`} role="radiogroup" aria-label="Brush">
                  {(["paint", "erase"] as const).map((b) => (
                    <button
                      key={b}
                      className={`${styles.chip} ${styles.segment}`}
                      role="radio"
                      aria-checked={brush === b}
                      title={b === "paint" ? "Drag to seat the audience; Shift: a wider brush, Alt: rub out" : "Drag to take seats away; Shift: a wider brush"}
                      onClick={() => setBrush(b)}
                    >
                      {b === "paint" ? "Paint" : "Erase"}
                    </button>
                  ))}
                </div>
              )}
            </div>
          </div>
        </>
      ) : (
        <div className={styles.body} />
      )}
      <ResizeGrip className={styles.grip} />
    </div>
  );
}

const panelWidth = { initial: 240, min: 200, max: 480 };

// The players panel's right edge: drag it (or focus it and use the arrow keys) to make room
// for long names; a double click goes back to the usual width. Never wider than half the
// window (the CSS holds that while the window shrinks).
function PanelEdge({ width, onWidth }: { width: number; onWidth: (w: number) => void }) {
  const fit = (w: number) => Math.round(Math.max(panelWidth.min, Math.min(panelWidth.max, window.innerWidth / 2, w)));

  const onPointerDown = (e: PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    const edge = e.currentTarget;
    const start = {
      x: e.clientX,
      width: (edge.previousElementSibling as HTMLElement).offsetWidth,
    };
    edge.setPointerCapture(e.pointerId);
    const move = (ev: globalThis.PointerEvent) => onWidth(fit(start.width + ev.clientX - start.x));
    const up = () => {
      edge.removeEventListener("pointermove", move);
      edge.removeEventListener("pointerup", up);
      edge.removeEventListener("pointercancel", up);
    };
    edge.addEventListener("pointermove", move);
    edge.addEventListener("pointerup", up);
    edge.addEventListener("pointercancel", up);
  };

  const onKeyDown = (e: KeyboardEvent<HTMLDivElement>) => {
    const step = e.key === "ArrowLeft" ? -16 : e.key === "ArrowRight" ? 16 : 0;
    if (!step) return;
    e.preventDefault();
    onWidth(fit(width + step));
  };

  return (
    <div
      className={styles.panelEdge}
      role="separator"
      aria-orientation="vertical"
      aria-label="Players list width"
      aria-valuemin={panelWidth.min}
      aria-valuemax={panelWidth.max}
      aria-valuenow={width}
      tabIndex={0}
      onPointerDown={onPointerDown}
      onDoubleClick={() => onWidth(panelWidth.initial)}
      onKeyDown={onKeyDown}
    />
  );
}
