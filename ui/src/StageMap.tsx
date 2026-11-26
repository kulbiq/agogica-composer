import { useEffect, useRef, type KeyboardEvent, type PointerEvent } from "react";
import { beginGesture, useEvent, type Gesture, type Point } from "./bridge";
import type { SeatedStage } from "./players";
import styles from "./App.module.css";
import { wrapDegrees } from "./ease";
import type { Levels } from "./level";
import { badgeRadius, nodesOf, StagePainter, type MapNames, type MapView, type NodeKey, type View } from "./stagePainter";

export type { MapNames, MapView, NodeKey };

type Props = {
  stage: SeatedStage;
  selfId: number;
  selected: NodeKey | null;
  onSelect: (key: NodeKey) => void;
  view: MapView;
  names: MapNames;
};

const hitRadius = badgeRadius + 2; // px
const nudgeKeys: Record<string, Point> = {
  ArrowLeft: { x: -1, y: 0 },
  ArrowRight: { x: 1, y: 0 },
  ArrowUp: { x: 0, y: 1 },
  ArrowDown: { x: 0, y: -1 },
};

const round = (v: number) => Math.round(v * 100) / 100;

// Q and E turn the listener (Binaural) 5 degrees, Shift 15; one gesture per key press.
const turnKeys: Record<string, number> = { q: -1, e: 1 };

export default function StageMap({ stage, selfId, selected, onSelect, view: mapView, names }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const textCopyRef = useRef<HTMLCanvasElement>(null);

  // The rAF loop reads the latest props through refs instead of restarting.
  const latest = useRef({ stage, selected, view: mapView, names });
  latest.current = { stage, selected, view: mapView, names };

  // Nodes the user is moving are drawn from here (exactly under the pointer)
  // until C++ reports the new position back.
  const overrides = useRef(new Map<NodeKey, Point>());
  const drag = useRef<{ key: NodeKey; gesture: Gesture; pointerId: number } | null>(null);
  const keyGesture = useRef<{ key: NodeKey; gesture: Gesture } | null>(null);
  const turn = useRef<{ gesture: Gesture; yaw: number } | null>(null); // drawn from here until the key is up
  const view = useRef<View>({ cx: 0, cy: 0, scale: 1, drawn: new Map() });
  const levels = useRef<Levels>({});
  useEvent("levels", (l) => (levels.current = l));
  const headYaw = useRef(0); // degrees clockwise; its own event, so the stage doesn't change every frame
  useEvent("headYaw", (yaw) => (headYaw.current = yaw));

  // A fresh stage from C++ supersedes local positions, except for a node still in a gesture.
  useEffect(() => {
    for (const key of overrides.current.keys())
      if (key !== drag.current?.key && key !== keyGesture.current?.key) overrides.current.delete(key);
  }, [stage]);

  useEffect(() => {
    const canvas = canvasRef.current!;
    const ctx = canvas.getContext("2d")!;
    let W = 0;
    let H = 0;
    let frame = 0;
    const t0 = performance.now();

    // Canvas colours follow the theme through the --ink-rgb token.
    const readInk = () => getComputedStyle(canvas).getPropertyValue("--ink-rgb").trim() || "15,23,42";
    const readPaper = () => getComputedStyle(canvas).getPropertyValue("--paper").trim() || "#f1f5f9";
    let ink = readInk();
    let paper = readPaper();
    const theme = window.matchMedia("(prefers-color-scheme: dark)");
    const onTheme = () => {
      ink = readInk();
      paper = readPaper();
    };
    theme.addEventListener("change", onTheme);

    let dpr = 0; // the pixel ratio the canvas was last sized for
    let lastPaint = -Infinity;
    const resize = () => {
      lastPaint = -Infinity; // sizing a canvas clears it: draw the next frame whatever the pace
      const r = canvas.getBoundingClientRect();
      dpr = window.devicePixelRatio || 1;
      W = r.width;
      H = r.height;
      canvas.width = Math.round(W * dpr);
      canvas.height = Math.round(H * dpr);
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    };
    resize();
    const ro = new ResizeObserver(resize);
    ro.observe(canvas);

    const painter = new StagePainter(textCopyRef.current!);
    // Levels arrive 30 times a second and the ring breathes slowly, so a resting map is drawn
    // 30 times a second; anything being moved, by the user or by easing, gets every frame.
    const restingFrameMs = 1000 / 30;
    const draw = (now: number) => {
      frame = requestAnimationFrame(draw);
      const moving = !painter.settled || drag.current !== null || keyGesture.current !== null || turn.current !== null;
      if (!moving && now - lastPaint < restingFrameMs - 4) return; // 4 ms: frames don't arrive exactly on time
      lastPaint = now;
      // The window moved to a screen with another pixel ratio: no resize event tells us.
      if ((window.devicePixelRatio || 1) !== dpr) resize();
      const s = latest.current.stage;
      view.current = painter.paint(ctx, {
        width: W,
        height: H,
        seconds: (now - t0) / 1000,
        ...latest.current,
        overrides: overrides.current,
        levels: levels.current,
        // What is heard: stereo speakers don't turn, so the arrow doesn't either.
        facing: s.outputMode === "binaural" ? (turn.current?.yaw ?? s.listener.yaw) + headYaw.current : 0,
        ink,
        paper,
      });
    };
    frame = requestAnimationFrame(draw);

    return () => {
      cancelAnimationFrame(frame);
      ro.disconnect();
      theme.removeEventListener("change", onTheme);
    };
  }, []);

  const toMetres = (e: PointerEvent): Point => {
    const r = canvasRef.current!.getBoundingClientRect();
    const { cx, cy, scale } = view.current;
    const half = stage.room / 2;
    const clamp = (v: number) => Math.min(half, Math.max(-half, v));
    return {
      x: round(clamp((e.clientX - r.left - cx) / scale)),
      y: round(clamp(-(e.clientY - r.top - cy) / scale)),
    };
  };

  const hitTest = (e: PointerEvent): NodeKey | null => {
    const r = canvasRef.current!.getBoundingClientRect();
    const { cx, cy, scale, drawn } = view.current;
    let best: NodeKey | null = null;
    let bestDistance = hitRadius;
    for (const [key, p] of drawn) {
      const d = Math.hypot(cx + p.x * scale - (e.clientX - r.left), cy - p.y * scale - (e.clientY - r.top));
      if (d <= bestDistance) {
        best = key;
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

  // One drag at a time, with the primary button (or one finger): a right click, a second
  // finger or a second mouse button neither starts a drag nor ends the one in progress.
  const onPointerDown = (e: PointerEvent<HTMLCanvasElement>) => {
    if (e.button !== 0 || drag.current) return;
    const key = hitTest(e);
    if (key === null) return;
    onSelect(key);
    endKeyGesture(); // the same node's parameters can't be in two gestures
    e.currentTarget.setPointerCapture(e.pointerId);
    e.currentTarget.style.cursor = "grabbing";
    drag.current = { key, gesture: startGesture(key), pointerId: e.pointerId };
  };

  const onPointerMove = (e: PointerEvent<HTMLCanvasElement>) => {
    if (drag.current?.pointerId === e.pointerId) move(drag.current.key, drag.current.gesture, toMetres(e));
    else if (!drag.current) e.currentTarget.style.cursor = hitTest(e) !== null ? "grab" : "";
  };

  // Released, cancelled, or the capture taken away (the window lost the pointer): the drag ends.
  const onPointerEnd = (e: PointerEvent<HTMLCanvasElement>) => {
    if (drag.current?.pointerId !== e.pointerId) return;
    drag.current.gesture.end();
    drag.current = null;
    e.currentTarget.style.cursor = hitTest(e) !== null ? "grab" : "";
  };

  // Arrow keys nudge the selected node 0.1 m (Shift: 1 m); one gesture per key press.
  const onKeyDown = (e: KeyboardEvent) => {
    const side = turnKeys[e.key.toLowerCase()];
    if (side && selected === "listener" && stage.outputMode === "binaural" && !e.metaKey && !e.ctrlKey) {
      e.preventDefault();
      turn.current ??= { gesture: beginGesture(selfId, ["listenerYaw"]), yaw: stage.listener.yaw };
      turn.current.yaw = wrapDegrees(turn.current.yaw + side * (e.shiftKey ? 15 : 5));
      turn.current.gesture.change({ listenerYaw: turn.current.yaw });
      return;
    }

    const dir = nudgeKeys[e.key];
    if (!dir || selected === null || drag.current) return;
    e.preventDefault();

    if (keyGesture.current?.key !== selected) {
      keyGesture.current?.gesture.end();
      keyGesture.current = { key: selected, gesture: startGesture(selected) };
    }

    const current = overrides.current.get(selected) ?? nodesOf(stage).get(selected)!;
    const step = e.shiftKey ? 1 : 0.1;
    const half = stage.room / 2;
    const clamp = (v: number) => round(Math.min(half, Math.max(-half, v)));
    move(selected, keyGesture.current.gesture, { x: clamp(current.x + dir.x * step), y: clamp(current.y + dir.y * step) });
  };

  const endKeyGesture = () => {
    keyGesture.current?.gesture.end();
    keyGesture.current = null;
  };
  const endTurn = () => {
    turn.current?.gesture.end();
    turn.current = null;
  };

  return (
    <>
      <canvas ref={textCopyRef} hidden aria-hidden />
      <canvas
        ref={canvasRef}
        className={styles.map}
        tabIndex={0}
        aria-label="Stage map. Arrow keys move the selected node, Shift for 1 m steps. Q and E turn the listener in Binaural."
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerEnd}
        onPointerCancel={onPointerEnd}
        onLostPointerCapture={onPointerEnd}
        onKeyDown={onKeyDown}
        onKeyUp={(e) => {
          if (nudgeKeys[e.key]) endKeyGesture();
          if (turnKeys[e.key.toLowerCase()]) endTurn();
        }}
        onBlur={() => {
          endKeyGesture();
          endTurn();
        }}
      />
    </>
  );
}
