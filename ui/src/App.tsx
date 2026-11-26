import { useCallback, useEffect, useMemo, useState, type CSSProperties, type KeyboardEvent, type PointerEvent } from "react";
import styles from "./App.module.css";
import { useStage } from "./bridge";
import { seat } from "./players";
import PlayersPanel from "./PlayersPanel";
import { useNumberPref, usePref } from "./prefs";
import ResizeGrip from "./ResizeGrip";
import TelemetryConsent from "./TelemetryConsent";
import StageMap, { type MapNames, type MapView, type NodeKey, type SeatTool, type Tool } from "./StageMap";
import TopBar from "./TopBar";

const views = ["grid", "azimuth"] as const satisfies readonly MapView[];
const nameModes = ["selected", "all"] as const satisfies readonly MapNames[];
const pointerTools = ["select", "drag"] as const satisfies readonly Tool[];
const seatTools = ["select", "paint", "erase"] as const satisfies readonly SeatTool[];

export default function App() {
  const snapshot = useStage();
  const stage = useMemo(() => snapshot && seat(snapshot), [snapshot]); // a player's tracks as one
  const selfId = stage?.players.find((p) => p.self)?.id;
  const [picks, setPicks] = useState<NodeKey[] | null>(null);
  const [view, setView] = usePref<MapView>("mapView", "grid", views);
  const [names, setNames] = usePref<MapNames>("mapNames", "selected", nameModes);
  const [in3d, setIn3d] = useState(false); // not saved: the editor always opens on the map
  const [tool, setTool] = useState<Tool>("drag"); // not saved: the editor always opens on Drag
  const [seatTool, setSeatTool] = useState<SeatTool | null>(null); // editing the audience, with what
  const [seatsPicked, setSeatsPicked] = useState(0);
  const [eraseRequest, setEraseRequest] = useState(0);
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
                tool={tool}
                seatTool={seatTool}
                eraseRequest={eraseRequest}
                onSeatsPicked={setSeatsPicked}
                top={toolsBottom + 10} // the gap the map has always had under one row
                fitRequest={fitRequest}
                onZoomed={setZoomed}
              />
              <div className={`${styles.mapTools} ${in3d ? styles.mapToolsRoom : ""}`} ref={tools}>
                <div className={styles.segments} role="radiogroup" aria-label="View">
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
                      setSeatTool(null); // the audience is edited on the map
                    }}
                  >
                    3D
                  </button>
                </div>
                {!in3d && !seatTool && (
                  <div className={styles.segments} role="radiogroup" aria-label="Pointer">
                    {pointerTools.map((t) => (
                      <button
                        key={t}
                        className={`${styles.chip} ${styles.segment}`}
                        role="radio"
                        aria-checked={tool === t}
                        title={
                          t === "select"
                            ? "Drag a rectangle to select the players inside it"
                            : "Drag a player to move it, empty space to move the view"
                        }
                        onClick={() => setTool(t)}
                      >
                        {t === "select" ? "Select" : "Drag"}
                      </button>
                    ))}
                  </div>
                )}
                {!in3d && zoomed && (
                  <button className={`${styles.chip} ${styles.segment}`} title="Show the whole room" onClick={() => setFitRequest((n) => n + 1)}>
                    Fit
                  </button>
                )}
                {!in3d && (
                  <div className={`${styles.segments} ${styles.toolsEnd}`}>
                    {seatTool && (
                      // The audience's tools. With seats selected, Erase takes exactly those away
                      // (and says how many); otherwise it is the brush that rubs seats out.
                      <div className={styles.segments} role="radiogroup" aria-label="Audience tool">
                        {seatTools.map((t) => {
                          const eraseSelected = t === "erase" && seatsPicked > 0;
                          return (
                            <button
                              key={t}
                              className={`${styles.chip} ${styles.segment}`}
                              role="radio"
                              aria-checked={seatTool === t && !eraseSelected}
                              title={
                                t === "select"
                                  ? "Drag a rectangle (or click) to select seats; Shift adds. Erase or Delete takes them away"
                                  : t === "paint"
                                    ? "Drag to seat the audience; Shift: a wider brush, Alt: rub out"
                                    : eraseSelected
                                      ? `Take away the ${seatsPicked} selected seats (Delete)`
                                      : "Drag to take seats away; Shift: a wider brush"
                              }
                              onClick={() => (eraseSelected ? setEraseRequest((n) => n + 1) : setSeatTool(t))}
                            >
                              {t === "select" ? "Select" : t === "paint" ? "Paint" : eraseSelected ? `Erase ${seatsPicked}` : "Erase"}
                            </button>
                          );
                        })}
                      </div>
                    )}
                    <button
                      className={`${styles.chip} ${styles.segment}`}
                      aria-pressed={seatTool !== null}
                      title={seatTool ? "Done with the audience" : "Edit where the audience sits"}
                      onClick={() => setSeatTool(seatTool ? null : "paint")}
                    >
                      Audience
                    </button>
                  </div>
                )}
              </div>
              {stage.telemetry === "ask" && <TelemetryConsent />}
            </div>
          </div>
        </>
      ) : (
        // Until the plugin has answered (a moment), or while a new track is taking its seat.
        <div className={`${styles.body} ${styles.elsewhere}`} role="status">
          <span className={`${styles.label} ${styles.waiting}`}>Connecting to Composer…</span>
        </div>
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
