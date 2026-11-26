import { useEffect, useMemo, useRef, useState, type KeyboardEvent, type PointerEvent } from "react";
import { cellOf, decodeAudience, emptyAudience, encodeAudience, hasSeat, seatPitch, setSeat, type Audience } from "./audience";
import { beginGesture, setAudience, setParams, useEvent, type Gesture, type Point } from "./bridge";
import type { SeatedStage } from "./players";
import styles from "./App.module.css";
import { wrapDegrees } from "./ease";
import type { Levels } from "./level";
import { walk } from "./room/camera";
import { fitted, moveAll, pan, toggle, within, zoomAt, type Zoom } from "./mapEdits";
import { readMaterials } from "./room/scene";
import { nodesOf, StagePainter, type MapNames, type MapView, type NodeKey, type View } from "./stagePainter";

export type { MapNames, MapView, NodeKey };

type Props = {
  stage: SeatedStage;
  selfId: number;
  selected: NodeKey | null; // the one that leads
  selection: readonly NodeKey[]; // all selected, the leading one last
  onSelect: (keys: NodeKey[]) => void;
  view: MapView;
  names: MapNames;
  in3d: boolean; // the room at the listener's eyes instead of the map
  onLeave3d: () => void;
  tool: Tool;
  audience: boolean; // editing the audience: Drag paints seats (Alt rubs out), Select picks them
  eraseRequest: number; // changes when the picked seats should go
  onSeatsPicked: (count: number) => void;
  top: number; // px of tools over the map's top edge
  fitRequest: number; // changes when the whole room should be shown again
  onZoomed: (zoomed: boolean) => void;
};

// What a drag on the map does. Drag: a node moves (all of the selected, when it is one of
// them), empty space pans, Shift draws a rectangle that selects. Select: a drag always draws a
// rectangle that selects what is inside (the seats while the audience is being edited, else the
// players); a click selects. Shift, Cmd or Ctrl add to the selection in both.
export type Tool = "drag" | "select";

// On the map: W A S D (Shift faster) move the listener.
const mapWalkKeys = new Set(["KeyW", "KeyA", "KeyS", "KeyD", "ShiftLeft", "ShiftRight"]);

const nudgeKeys: Record<string, Point> = {
  ArrowLeft: { x: -1, y: 0 },
  ArrowRight: { x: 1, y: 0 },
  ArrowUp: { x: 0, y: 1 },
  ArrowDown: { x: 0, y: -1 },
};

const round = (v: number) => Math.round(v * 100) / 100;
const wrap = (degrees: number) => ((((degrees + 180) % 360) + 360) % 360) - 180;

// Q and E turn the listener (Binaural) 5 degrees, Shift 15; one gesture per key press.
const turnKeys: Record<string, number> = { KeyQ: -1, KeyE: 1 };
// In 3D: walking and turning, held down (by physical key, whatever the keyboard layout).
const roomKeys = new Set(["KeyW", "KeyA", "KeyS", "KeyD", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "ShiftLeft", "ShiftRight"]);
const turnDegreesPerSecond = 90;
const lookDegreesPerPixel = 0.25;

// A press on the map: on empty space it pans the view, or draws a rectangle that selects
// (Shift, or the Select tool); a press on a selected node of several moves them all.
// `base` and `seatBase`: what was selected before (kept when adding), `hit`: the node under it.
type MapPress = {
  pointerId: number;
  kind: "pan" | "box";
  x0: number;
  y0: number;
  x: number;
  y: number;
  moved: boolean;
  base: readonly NodeKey[];
  seats: boolean; // the rectangle picks seats, not players
  seatBase: Audience;
  hit: NodeKey | null;
};
type Moving = { pointerId: number; key: NodeKey; from: Point; moved: boolean; nodes: Map<NodeKey, { gesture: Gesture; start: Point }> };
type Box = { x0: number; y0: number; x1: number; y1: number };

export default function StageMap(props: Props) {
  const { stage, selfId, selected, selection, onSelect, view: mapView, names, in3d, onLeave3d, tool, audience: editingAudience, eraseRequest, onSeatsPicked, top, fitRequest, onZoomed } = props;
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [standing, setStanding] = useState(false);

  // The rAF loop reads the latest props through refs instead of restarting.
  // The audience: as C++ has it, or as painted until C++ has that (each stroke is sent once,
  // when it ends).
  const saved = useMemo(() => decodeAudience(stage.audience), [stage.audience]);
  const stroke = useRef<{ pointerId: number; seats: Audience; on: boolean; big: boolean; last: Point } | null>(null);
  const painted = useRef<{ seats: Audience; sent: string } | null>(null);
  if (painted.current && painted.current.sent === stage.audience) painted.current = null;
  const audience = stroke.current?.seats ?? painted.current?.seats ?? saved;

  // Seats picked with the Select tool while editing the audience (drawn stronger; Erase takes them away).
  const picked = useRef<Audience>(emptyAudience());
  const pickedCount = useRef(0);
  const pick = (seats: Audience) => {
    picked.current = seats;
    let n = 0;
    for (const b of seats) for (let v = b; v; v &= v - 1) n++;
    pickedCount.current = n;
    onSeatsPicked(n);
    poked.current = performance.now();
  };

  const latest = useRef({ stage, selected, selection, view: mapView, names, in3d, standing, audience, painting: editingAudience, top });
  latest.current = { stage, selected, selection, view: mapView, names, in3d, standing, audience, painting: editingAudience, top };

  // Nodes the user is moving are drawn from here (exactly under the pointer)
  // until C++ reports the new position back.
  const overrides = useRef(new Map<NodeKey, Point>());
  const drag = useRef<Moving | null>(null);
  const keyGesture = useRef<{ id: string; gestures: Map<NodeKey, Gesture> } | null>(null); // arrow keys, for this selection
  const press = useRef<MapPress | null>(null);
  const [box, setBox] = useState<Box | null>(null);
  // The map's zoom (session only); `poked`: when it last changed, so frames follow the wheel.
  const zoom = useRef<Zoom>(fitted);
  const poked = useRef(-Infinity);
  const zoomTo = (z: Zoom) => {
    zoom.current = z;
    poked.current = performance.now();
    onZoomed(z.k > 1);
  };
  // listenerYaw: one gesture for every way of turning (Q/E, ←/→, dragging in 3D); drawn from
  // here until it ends.
  const turn = useRef<{ gesture: Gesture; yaw: number } | null>(null);
  // After a turn: the last direction written, drawn until C++ reports it back (or 1 s), so the
  // view doesn't jump back to the snapshot's older value.
  const turned = useRef<{ yaw: number; until: number } | null>(null);
  const walking = useRef<{ gesture: Gesture; at: Point } | null>(null);
  const held = useRef(new Set<string>()); // keys held down
  const lookDrag = useRef<{ pointerId: number; x: number; y: number; moved: boolean; hit: NodeKey | null } | null>(null);
  // Where the eyes look when nothing is written: up and down always, and left and right in
  // Stereo (its speakers don't turn, so neither does listenerYaw).
  const look = useRef({ yaw: 0, pitch: 0 });
  const view = useRef<View>({ cx: 0, cy: 0, scale: 1, flat: true, hits: [] });
  const levels = useRef<Levels>({});
  useEvent("levels", (l) => (levels.current = l));
  const headYaw = useRef(0); // degrees clockwise; its own event, so the stage doesn't change every frame
  useEvent("headYaw", (yaw) => (headYaw.current = yaw));

  // Turning writes listenerYaw only in Binaural (stereo speakers don't turn); otherwise it turns the view.
  const binaural = () => latest.current.stage.outputMode === "binaural";

  // Turns the listener (or in Stereo the view). The angle adds up unrounded; only what is sent
  // is rounded to the parameter's step.
  const turnBy = (degrees: number) => {
    if (!binaural()) {
      look.current.yaw = wrap(look.current.yaw + degrees);
      return;
    }
    turn.current ??= { gesture: beginGesture(selfId, ["listenerYaw"]), yaw: yaw() };
    turn.current.yaw = wrap(turn.current.yaw + degrees);
    turn.current.gesture.change({ listenerYaw: wrapDegrees(turn.current.yaw) });
  };
  const endTurn = () => {
    if (!turn.current) return;
    turn.current.gesture.end();
    turned.current = { yaw: turn.current.yaw, until: performance.now() + 1000 };
    turn.current = null;
  };
  const endWalk = () => {
    walking.current?.gesture.end();
    walking.current = null;
  };
  const endKeyGesture = () => {
    keyGesture.current?.gestures.forEach((g) => g.end());
    keyGesture.current = null;
  };
  // Straight ahead and level: listenerYaw 0 (one whole gesture) where turning writes it, else the
  // view's own direction.
  const faceFront = () => {
    look.current.pitch = 0;
    if (binaural()) {
      endTurn();
      setParams(selfId, { listenerYaw: 0 });
      turned.current = { yaw: 0, until: performance.now() + 1000 };
    } else look.current.yaw = 0;
    poked.current = performance.now(); // drawn at once
  };

  // Moves the listener (one gesture per walk) `seconds` of walking along `yawDegrees`.
  const walkListener = (input: { forward: number; right: number; faster: boolean }, yawDegrees: number, seconds: number) => {
    const s = latest.current.stage;
    walking.current ??= { gesture: beginGesture(selfId, ["listenerX", "listenerY"]), at: { ...(overrides.current.get("listener") ?? s.listener) } };
    walking.current.at = walk(walking.current.at, input, yawDegrees, seconds, s.room);
    overrides.current.set("listener", walking.current.at);
    walking.current.gesture.change({ listenerX: round(walking.current.at.x), listenerY: round(walking.current.at.y) });
  };

  const endAll = () => {
    held.current.clear();
    lookDrag.current = null;
    endWalk();
    endTurn();
    endKeyGesture();
  };

  // A fresh stage from C++ supersedes local positions, except for a node still in a gesture.
  useEffect(() => {
    for (const key of overrides.current.keys())
      if (!drag.current?.nodes.has(key) && !keyGesture.current?.gestures.has(key) && !(key === "listener" && walking.current))
        overrides.current.delete(key);
  }, [stage]);

  // Into the room: the keys go to the map. Out of it, or gone: every gesture ends.
  useEffect(() => {
    if (in3d) canvasRef.current?.focus();
    else endAll();
    return endAll;
  }, [in3d]); // endAll touches refs only

  // No more audience editing: nothing stays picked.
  useEffect(() => {
    if (!editingAudience && pickedCount.current) pick(emptyAudience());
  }, [editingAudience]);

  useEffect(() => {
    if (eraseRequest) erasePicked();
  }, [eraseRequest]);

  useEffect(() => {
    if (fitRequest) zoomTo(fitted);
  }, [fitRequest]);

  // A smaller room: the zoomed view stays inside it.
  useEffect(() => zoomTo(pan(zoom.current, 0, 0, stage.room / 2)), [stage.room]);

  // The wheel (or a pinch, which arrives as a wheel with Ctrl) zooms the map at the pointer.
  // Not React's onWheel: that one is passive, and the page must not take the wheel too.
  useEffect(() => {
    const canvas = canvasRef.current!;
    const onWheel = (e: WheelEvent) => {
      if (latest.current.in3d || !view.current.flat) return;
      e.preventDefault();
      const unit = e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? 400 : 1; // lines, pages
      const factor = Math.exp(-e.deltaY * unit * (e.ctrlKey ? 0.01 : 0.002));
      zoomTo(zoomAt(zoom.current, factor, pointerMetres(e), latest.current.stage.room / 2));
    };
    canvas.addEventListener("wheel", onWheel, { passive: false });
    return () => canvas.removeEventListener("wheel", onWheel);
  }, []); // reads everything through refs

  useEffect(() => {
    const canvas = canvasRef.current!;
    let W = 0;
    let H = 0;
    let frame = 0;
    const t0 = performance.now();
    let last = t0;

    // Canvas colours follow the theme through the --ink-rgb token and styles/room.css.
    let ink = "";
    let paper = "";
    let materials = readMaterials(getComputedStyle(canvas));
    const readTheme = () => {
      const style = getComputedStyle(canvas);
      ink = style.getPropertyValue("--ink-rgb").trim() || "15,23,42";
      paper = style.getPropertyValue("--paper").trim() || "#f1f5f9";
      materials = readMaterials(style);
    };
    readTheme();
    const theme = window.matchMedia("(prefers-color-scheme: dark)");
    theme.addEventListener("change", readTheme);
    const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

    let dpr = 0; // the pixel ratio the canvases are drawn for (the painter sizes them)
    let lastPaint = -Infinity;
    const resize = () => {
      lastPaint = -Infinity; // draw the next frame whatever the pace
      const r = canvas.getBoundingClientRect();
      dpr = window.devicePixelRatio || 1;
      W = r.width;
      H = r.height;
    };
    resize();
    const ro = new ResizeObserver(resize);
    ro.observe(canvas);

    const painter = new StagePainter(canvas);
    // Levels arrive 30 times a second and the ring breathes slowly, so a resting map is looked
    // at 30 times a second (and drawn only if something in it changed); anything being moved,
    // by the user or by easing, 60 times a second. Not every frame: a 120 Hz screen would draw
    // twice as many, for motion the eye can't tell apart, at twice the cost.
    const restingFrameMs = 1000 / 30;
    const movingFrameMs = 1000 / 60;
    const draw = (now: number) => {
      frame = requestAnimationFrame(draw);
      const dt = Math.min(0.05, (now - last) / 1000);
      last = now;
      const { stage: s, in3d: room } = latest.current;

      // Walking and turning with held keys: in the room once the camera is down there, along
      // where it looks; on the map, W up the map.
      const keys = held.current;
      const forward = Number(keys.has("KeyW") || keys.has("ArrowUp")) - Number(keys.has("KeyS") || keys.has("ArrowDown"));
      const right = Number(keys.has("KeyD")) - Number(keys.has("KeyA"));
      const faster = keys.has("ShiftLeft") || keys.has("ShiftRight");
      if (room && painter.inRoom) {
        const turning = Number(keys.has("ArrowRight")) - Number(keys.has("ArrowLeft"));
        if (turning) turnBy(turning * turnDegreesPerSecond * dt);
        if (forward || right) walkListener({ forward, right, faster }, yaw(), dt);
        else endWalk();
      } else if (!room && view.current.flat) {
        if (forward || right) walkListener({ forward, right, faster }, 0, dt);
        else endWalk();
      }

      const moving =
        !painter.settled ||
        drag.current ||
        press.current ||
        stroke.current ||
        keyGesture.current ||
        turn.current ||
        walking.current ||
        lookDrag.current ||
        held.current.size > 0 ||
        now - poked.current < 150;
      if (now - lastPaint < (moving ? movingFrameMs : restingFrameMs) - 4) return; // 4 ms: frames don't arrive exactly on time
      lastPaint = now;
      // The window moved to a screen with another pixel ratio: no resize event tells us.
      if ((window.devicePixelRatio || 1) !== dpr) resize();
      const head = s.headTracking.on ? headYaw.current : 0;
      const heard = s.outputMode === "binaural" ? yaw() + head : 0; // what the ears face
      view.current = painter.paint({
        width: W,
        height: H,
        dpr,
        seconds: (now - t0) / 1000,
        ...latest.current,
        audience: stroke.current?.seats ?? painted.current?.seats ?? latest.current.audience, // the stroke as it goes
        picked: latest.current.painting ? picked.current : undefined,
        overrides: overrides.current,
        zoom: zoom.current,
        levels: levels.current,
        // Stereo speakers don't turn, so the arrow doesn't either.
        facing: heard,
        look: {
          on: room,
          yaw: s.outputMode === "binaural" ? heard : look.current.yaw,
          pitch: look.current.pitch,
          standing: latest.current.standing,
        },
        ink,
        paper,
        materials,
        reducedMotion: reducedMotion.matches,
      });
    };
    frame = requestAnimationFrame(draw);

    return () => {
      cancelAnimationFrame(frame);
      painter.dispose();
      ro.disconnect();
      theme.removeEventListener("change", readTheme);
    };
  }, []); // reads everything through refs

  // The listener's direction as written (Binaural) or looked (Stereo), degrees clockwise.
  const yaw = () => {
    if (!binaural()) return look.current.yaw;
    if (turn.current) return turn.current.yaw;
    const reported = latest.current.stage.listener.yaw;
    const t = turned.current;
    if (t && performance.now() < t.until && Math.abs(wrap(reported - t.yaw)) > 0.06) return t.yaw;
    turned.current = null;
    return reported;
  };

  // Where the pointer is in the room, in metres (not kept inside it).
  const pointerMetres = (e: { clientX: number; clientY: number }): Point => {
    const r = canvasRef.current!.getBoundingClientRect();
    const { cx, cy, scale } = view.current;
    return { x: (e.clientX - r.left - cx) / scale, y: -(e.clientY - r.top - cy) / scale };
  };

  // On the map, the node nearest the pointer within reach; in the room, the nearest one in depth
  // whose badge is under the pointer.
  const hitTest = (e: PointerEvent): NodeKey | null => {
    const r = canvasRef.current!.getBoundingClientRect();
    const x = e.clientX - r.left;
    const y = e.clientY - r.top;
    const { hits, flat } = view.current;
    if (!flat) return hits.find((h) => Math.hypot(h.x - x, h.y - y) <= h.r)?.key ?? null;
    let best: NodeKey | null = null;
    let bestDistance = Infinity;
    for (const h of hits) {
      const d = Math.hypot(h.x - x, h.y - y);
      if (d <= h.r && d < bestDistance) {
        best = h.key;
        bestDistance = d;
      }
    }
    return best;
  };

  const startGesture = (key: NodeKey) =>
    key === "listener"
      ? beginGesture(selfId, ["listenerX", "listenerY"])
      : beginGesture(key, ["playerX", "playerY"]);

  const move = (key: NodeKey, gesture: Gesture, p: Point) => {
    overrides.current.set(key, p);
    gesture.change(key === "listener" ? { listenerX: p.x, listenerY: p.y } : { playerX: p.x, playerY: p.y });
  };

  const placeOf = (key: NodeKey) => overrides.current.get(key) ?? nodesOf(stage).get(key)!;

  // One drag at a time, with the primary button (or one finger): a right click, a second
  // finger or a second mouse button neither starts a drag nor ends the one in progress.
  // On the map, see `Tool`; editing the audience, Drag paints seats instead (Alt rubs out,
  // Shift a 3 x 3 brush). In the room (and while the camera swoops) a drag looks around, and a
  // click on a badge selects it.
  const onPointerDown = (e: PointerEvent<HTMLCanvasElement>) => {
    if (e.button !== 0 || drag.current || lookDrag.current || press.current) return;
    const key = hitTest(e);
    if (in3d || !view.current.flat) {
      e.currentTarget.setPointerCapture(e.pointerId);
      lookDrag.current = { pointerId: e.pointerId, x: e.clientX, y: e.clientY, moved: false, hit: key };
      return;
    }
    e.currentTarget.setPointerCapture(e.pointerId);
    if (editingAudience && tool === "drag") {
      const at = pointerMetres(e);
      stroke.current = { pointerId: e.pointerId, seats: audience.slice(), on: !e.altKey, big: e.shiftKey, last: at };
      paintTo(stroke.current, at);
      return;
    }
    const extend = e.shiftKey || e.metaKey || e.ctrlKey;
    if (key === null || tool === "select") {
      const at = { x: e.clientX, y: e.clientY };
      press.current = {
        pointerId: e.pointerId,
        kind: tool === "select" || e.shiftKey ? "box" : "pan",
        x0: at.x,
        y0: at.y,
        ...at,
        moved: false,
        base: extend ? selection : [],
        seats: editingAudience,
        seatBase: extend ? picked.current.slice() : emptyAudience(),
        hit: key,
      };
      return;
    }
    if (extend) {
      onSelect(toggle(selection, key));
      return;
    }
    const group = selection.length > 1 && selection.includes(key) ? selection : [key];
    if (group.length === 1) onSelect([key]);
    endKeyGesture(); // the same node's parameters can't be in two gestures
    e.currentTarget.style.cursor = "grabbing";
    drag.current = {
      pointerId: e.pointerId,
      key,
      from: pointerMetres(e),
      moved: false,
      nodes: new Map(group.map((k) => [k, { gesture: startGesture(k), start: placeOf(k) }])),
    };
  };

  const onPointerMove = (e: PointerEvent<HTMLCanvasElement>) => {
    const l = lookDrag.current;
    if (l?.pointerId === e.pointerId) {
      if (!l.moved && Math.hypot(e.clientX - l.x, e.clientY - l.y) < 4) return; // still a click
      l.moved = true;
      e.currentTarget.style.cursor = "grabbing";
      if (e.clientX !== l.x) turnBy((e.clientX - l.x) * lookDegreesPerPixel);
      look.current.pitch = Math.max(-40, Math.min(30, look.current.pitch - (e.clientY - l.y) * 0.2));
      l.x = e.clientX;
      l.y = e.clientY;
      return;
    }

    const s = stroke.current;
    if (s?.pointerId === e.pointerId) {
      paintTo(s, pointerMetres(e));
      return;
    }

    const d = drag.current;
    if (d?.pointerId === e.pointerId) {
      const at = pointerMetres(e);
      if (!d.moved && Math.hypot(at.x - d.from.x, at.y - d.from.y) * view.current.scale < 3) return; // still a click
      d.moved = true;
      const starts = new Map([...d.nodes].map(([k, n]) => [k, n.start]));
      const places = moveAll(starts, { x: at.x - d.from.x, y: at.y - d.from.y }, stage.room / 2);
      for (const [k, p] of places) move(k, d.nodes.get(k)!.gesture, p);
      return;
    }

    const p = press.current;
    if (p?.pointerId === e.pointerId) {
      if (!p.moved && Math.hypot(e.clientX - p.x0, e.clientY - p.y0) < 3) return;
      p.moved = true;
      if (p.kind === "pan") {
        const { scale } = view.current;
        zoomTo(pan(zoom.current, (e.clientX - p.x) / scale, -(e.clientY - p.y) / scale, stage.room / 2));
        e.currentTarget.style.cursor = "grabbing";
      } else {
        const r = e.currentTarget.getBoundingClientRect();
        const a = { x: p.x0 - r.left, y: p.y0 - r.top };
        const b = { x: e.clientX - r.left, y: e.clientY - r.top };
        setBox({ x0: Math.min(a.x, b.x), y0: Math.min(a.y, b.y), x1: Math.max(a.x, b.x), y1: Math.max(a.y, b.y) });
        if (p.seats) pick(seatsWithin(p.seatBase, { clientX: p.x0, clientY: p.y0 }, e));
        else {
          const inside = within(view.current.hits, a, b);
          onSelect([...p.base.filter((k) => !inside.includes(k as number)), ...inside]);
        }
      }
      p.x = e.clientX;
      p.y = e.clientY;
      return;
    }

    if (!in3d) e.currentTarget.style.cursor = editingAudience || tool === "select" ? "crosshair" : hitTest(e) !== null ? "grab" : "";
  };

  // The seats between two pointer positions (a rectangle on the map), added to `base`.
  const seatsWithin = (base: Audience, from: { clientX: number; clientY: number }, to: { clientX: number; clientY: number }) => {
    const out = base.slice();
    const a = pointerMetres(from);
    const b = pointerMetres(to);
    const i0 = Math.ceil(Math.min(a.x, b.x) / seatPitch - 0.5);
    const i1 = Math.floor(Math.max(a.x, b.x) / seatPitch + 0.5);
    const j0 = Math.ceil(Math.min(a.y, b.y) / seatPitch - 0.5);
    const j1 = Math.floor(Math.max(a.y, b.y) / seatPitch + 0.5);
    for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) if (hasSeat(audience, i, j)) setSeat(out, i, j, true);
    return out;
  };

  // Takes the picked seats away (one edit), and nothing stays picked.
  const erasePicked = () => {
    if (!pickedCount.current) return;
    const seats = audience.slice();
    const gone = picked.current;
    for (let k = 0; k < seats.length; k++) seats[k] &= ~gone[k];
    pick(emptyAudience());
    const sent = encodeAudience(seats);
    if (sent !== (painted.current?.sent ?? stage.audience)) {
      painted.current = { seats, sent };
      void setAudience(sent);
    }
  };

  // Paints (or rubs out) every seat the pointer passed over since the last move.
  const paintTo = (s: NonNullable<typeof stroke.current>, to: Point) => {
    const steps = Math.max(1, Math.ceil(Math.hypot(to.x - s.last.x, to.y - s.last.y) / (seatPitch / 3)));
    const reach = s.big ? 1 : 0;
    for (let n = 1; n <= steps; n++) {
      const cell = cellOf({ x: s.last.x + ((to.x - s.last.x) * n) / steps, y: s.last.y + ((to.y - s.last.y) * n) / steps });
      if (!cell) continue;
      for (let dj = -reach; dj <= reach; dj++) for (let di = -reach; di <= reach; di++) setSeat(s.seats, cell[0] + di, cell[1] + dj, s.on);
    }
    s.last = to;
  };

  // Released, cancelled, or the capture taken away (the window lost the pointer): the drag ends.
  const onPointerEnd = (e: PointerEvent<HTMLCanvasElement>) => {
    const l = lookDrag.current;
    if (l?.pointerId === e.pointerId) {
      lookDrag.current = null;
      if (!l.moved && l.hit !== null && l.hit !== "listener" && e.type === "pointerup") onSelect([l.hit]);
      if (![...held.current].some((k) => k === "ArrowLeft" || k === "ArrowRight")) endTurn();
      e.currentTarget.style.cursor = in3d ? "grab" : "";
      return;
    }

    const s = stroke.current;
    if (s?.pointerId === e.pointerId) {
      stroke.current = null;
      const sent = encodeAudience(s.seats);
      if (sent !== (painted.current?.sent ?? stage.audience)) {
        painted.current = { seats: s.seats, sent };
        void setAudience(sent);
      }
      return;
    }

    const d = drag.current;
    if (d?.pointerId === e.pointerId) {
      d.nodes.forEach((n) => n.gesture.end());
      drag.current = null;
      // A click on one of several selected: that one alone.
      if (!d.moved && d.nodes.size > 1 && e.type === "pointerup") onSelect([d.key]);
      e.currentTarget.style.cursor = hitTest(e) !== null ? "grab" : "";
      return;
    }

    const p = press.current;
    if (p?.pointerId === e.pointerId) {
      press.current = null;
      setBox(null);
      e.currentTarget.style.cursor = "";
      if (p.moved || e.type !== "pointerup") return;
      const extend = e.shiftKey || e.metaKey || e.ctrlKey;
      if (p.seats) {
        // A click picks the seat under it (or with Shift, Cmd or Ctrl adds it); on no seat, none.
        const cell = cellOf(pointerMetres(e));
        const next = extend ? picked.current.slice() : emptyAudience();
        if (cell && hasSeat(audience, cell[0], cell[1])) setSeat(next, cell[0], cell[1], !(extend && hasSeat(picked.current, cell[0], cell[1])));
        pick(next);
      } else if (p.hit !== null) onSelect(extend ? toggle(selection, p.hit) : [p.hit]); // the Select tool's click
      // A click on empty space: back to the leading node alone.
      else if (p.base.length === 0 && selected !== null) onSelect([selected]);
    }
  };

  const onKeyDown = (e: KeyboardEvent) => {
    if (e.metaKey || e.ctrlKey || e.altKey) {
      // The host's and the system's shortcuts. macOS sends no keyup for keys released while
      // Cmd is down, so nothing stays held.
      held.current.clear();
      return;
    }

    // Delete or Backspace: the picked seats go; Escape: none picked.
    if (!in3d && pickedCount.current && (e.code === "Delete" || e.code === "Backspace")) {
      e.preventDefault();
      erasePicked();
      return;
    }
    if (!in3d && pickedCount.current && e.code === "Escape") {
      e.preventDefault();
      pick(emptyAudience());
      return;
    }

    // R: straight ahead again.
    if (e.code === "KeyR") {
      e.preventDefault();
      if (!e.repeat) faceFront();
      return;
    }

    if (in3d) {
      if (e.code === "Escape") {
        e.preventDefault();
        onLeave3d();
      } else if (e.code === "Space") {
        e.preventDefault();
        if (!e.repeat) setStanding(!standing);
      } else if (roomKeys.has(e.code)) {
        e.preventDefault();
        held.current.add(e.code);
      }
    } else if (e.code === "Escape" && selection.length > 1 && selected !== null) {
      e.preventDefault();
      onSelect([selected]); // several selected: back to the leading one
      return;
    } else if (mapWalkKeys.has(e.code)) {
      if (!e.code.startsWith("Shift")) e.preventDefault();
      held.current.add(e.code);
      if (!e.code.startsWith("Shift")) return;
    }

    // Q and E: the selected listener on the map, the listener you are in the room.
    const side = turnKeys[e.code];
    if (side && (in3d || selected === "listener") && stage.outputMode === "binaural") {
      e.preventDefault();
      turnBy(side * (e.shiftKey ? 15 : 5));
      return;
    }

    // Arrow keys nudge the selected nodes 0.1 m (Shift: 1 m); one gesture per key press.
    const dir = nudgeKeys[e.key];
    if (in3d || !dir || selection.length === 0 || drag.current) return;
    e.preventDefault();

    const id = selection.join();
    if (keyGesture.current?.id !== id) {
      endKeyGesture();
      keyGesture.current = { id, gestures: new Map(selection.map((k) => [k, startGesture(k)])) };
    }
    const step = e.shiftKey ? 1 : 0.1;
    const places = moveAll(new Map(selection.map((k) => [k, placeOf(k)])), { x: dir.x * step, y: dir.y * step }, stage.room / 2);
    for (const [k, p] of places) move(k, keyGesture.current.gestures.get(k)!, p);
  };

  const onKeyUp = (e: KeyboardEvent) => {
    held.current.delete(e.code);
    if (nudgeKeys[e.key]) endKeyGesture();
    const turning = held.current.has("ArrowLeft") || held.current.has("ArrowRight") || lookDrag.current;
    if ((turnKeys[e.code] || e.code === "ArrowLeft" || e.code === "ArrowRight") && !turning) endTurn();
  };

  return (
    <>
      <canvas
        ref={canvasRef}
        className={styles.map}
        tabIndex={0}
        aria-label={
          in3d
            ? "The room at the listener's eyes. W A S D walk, Shift faster, drag or arrow keys to look around, R to face the front, Space to stand up or sit down, Escape for the map."
            : "Stage map. W A S D move the listener, arrow keys move the selected nodes, Shift for 1 m steps. Shift-click selects several, Shift-drag or the Select tool selects in a rectangle, Delete takes away selected seats, the wheel zooms. Q and E turn the listener in Binaural, R faces it front."
        }
        style={in3d ? { cursor: "grab" } : undefined}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerEnd}
        onPointerCancel={onPointerEnd}
        onLostPointerCapture={onPointerEnd}
        onKeyDown={onKeyDown}
        onKeyUp={onKeyUp}
        onBlur={endAll}
      />
      {box && <div className={styles.box} style={{ left: box.x0, top: box.y0, width: box.x1 - box.x0, height: box.y1 - box.y0 }} />}
      {in3d && (
        <div className={styles.keys}>
          <span className={styles.key}>
            <b>W A S D</b>Walk
          </span>
          <span className={styles.key}>
            <b>Shift</b>Faster
          </span>
          <span className={styles.key}>
            <b>Drag</b>
            {stage.outputMode === "binaural" ? "Turn" : "Look around"}
          </span>
          <button
            className={`${styles.key} ${styles.keyButton}`}
            title="Look straight ahead"
            onMouseDown={(e) => e.preventDefault()} // the keys stay with the room
            onClick={faceFront}
          >
            <b>R</b>Face front
          </button>
          <span className={styles.key}>
            <b>Space</b>
            {standing ? "Sit down" : "Stand up"}
          </span>
          <span className={styles.key}>
            <b>Esc</b>Map
          </span>
        </div>
      )}
    </>
  );
}
