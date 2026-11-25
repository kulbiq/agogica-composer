import { useEffect, useRef, type KeyboardEvent, type PointerEvent } from "react";
import { beginGesture, useJuceEvent, type Gesture, type Point, type Stage } from "./juce";
import styles from "./App.module.css";
import { levelOf, type Levels } from "./level";

export type NodeKey = "listener" | number;

type Props = {
  stage: Stage;
  selfId: number;
  selected: NodeKey | null;
  onSelect: (key: NodeKey) => void;
};

const margin = 32; // px around the room square
const hitRadius = 12; // px
const nudgeKeys: Record<string, Point> = {
  ArrowLeft: { x: -1, y: 0 },
  ArrowRight: { x: 1, y: 0 },
  ArrowUp: { x: 0, y: 1 },
  ArrowDown: { x: 0, y: -1 },
};

// (1 - cos) / 2: eases at the extremes, so motion breathes rather than ticks.
const breath = (t: number, period: number) => (1 - Math.cos(((t % period) / period) * Math.PI * 2)) / 2;
const round = (v: number) => Math.round(v * 100) / 100;

function nodesOf(stage: Stage): Map<NodeKey, Point> {
  const nodes = new Map<NodeKey, Point>([["listener", stage.listener]]);
  for (const p of stage.players) nodes.set(p.id, p);
  return nodes;
}

export default function StageMap({ stage, selfId, selected, onSelect }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  // The rAF loop reads the latest props through refs instead of restarting.
  const latest = useRef({ stage, selected });
  latest.current = { stage, selected };

  // Nodes the user is moving are drawn from here (exactly under the pointer)
  // until C++ reports the new position back.
  const overrides = useRef(new Map<NodeKey, Point>());
  const drag = useRef<{ key: NodeKey; gesture: Gesture } | null>(null);
  const keyGesture = useRef<{ key: NodeKey; gesture: Gesture } | null>(null);
  const view = useRef({ cx: 0, cy: 0, scale: 1 });
  const levels = useRef<Levels>({});
  useJuceEvent<Levels>("levels", (l) => (levels.current = l));
  const headYaw = useRef(0); // degrees clockwise; its own event, so the stage doesn't change every frame
  useJuceEvent<number>("headYaw", (yaw) => (headYaw.current = yaw));

  // A fresh stage from C++ supersedes local positions, except for a node still in a gesture.
  useEffect(() => {
    for (const key of overrides.current.keys())
      if (key !== drag.current?.key && key !== keyGesture.current?.key) overrides.current.delete(key);
  }, [stage]);

  useEffect(() => {
    const canvas = canvasRef.current!;
    const ctx = canvas.getContext("2d")!;
    const drawn = new Map<NodeKey, Point>(); // eased positions in metres
    const shown = new Map<number, number>(); // eased input levels, 0..1
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

    const resize = () => {
      const r = canvas.getBoundingClientRect();
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      W = r.width;
      H = r.height;
      canvas.width = Math.round(W * dpr);
      canvas.height = Math.round(H * dpr);
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    };
    resize();
    const ro = new ResizeObserver(resize);
    ro.observe(canvas);

    const draw = (now: number) => {
      const t = (now - t0) / 1000;
      const { stage, selected } = latest.current;
      const half = stage.room / 2;
      const side = Math.max(0, Math.min(W, H) - margin * 2);
      const scale = side / stage.room;
      const cx = W / 2;
      const cy = H / 2;
      view.current = { cx, cy, scale };

      // Snap hairlines to the pixel grid so they stay crisp at 1x.
      const px = (v: number) => Math.round(v) + 0.5;
      const sx = (x: number) => cx + x * scale;
      const sy = (y: number) => cy - y * scale;

      ctx.clearRect(0, 0, W, H);
      ctx.lineWidth = 1;

      // Grid: 1 m minor (skipped when too dense), 5 m major, counted from the centre.
      for (let m = -Math.floor(half); m <= half; m++) {
        if (Math.abs(m) >= half) continue; // the outline covers the edges
        const major = m % 5 === 0;
        if (!major && scale < 6) continue;
        ctx.strokeStyle = `rgba(${ink},${major ? 0.1 : 0.05})`;
        ctx.beginPath();
        ctx.moveTo(px(sx(m)), sy(half));
        ctx.lineTo(px(sx(m)), sy(-half));
        ctx.moveTo(sx(-half), px(sy(m)));
        ctx.lineTo(sx(half), px(sy(m)));
        ctx.stroke();
      }

      ctx.strokeStyle = `rgba(${ink},0.2)`;
      ctx.strokeRect(px(sx(-half)), px(sy(half)), px(sx(half)) - px(sx(-half)), px(sy(-half)) - px(sy(half)));

      // Scale bar: 5 m, under the bottom-left corner.
      const barY = px(sy(-half) + 16);
      ctx.strokeStyle = `rgba(${ink},0.45)`;
      ctx.beginPath();
      ctx.moveTo(sx(-half), barY);
      ctx.lineTo(sx(-half) + 5 * scale, barY);
      ctx.moveTo(px(sx(-half)), barY - 3);
      ctx.lineTo(px(sx(-half)), barY + 3);
      ctx.moveTo(px(sx(-half) + 5 * scale), barY - 3);
      ctx.lineTo(px(sx(-half) + 5 * scale), barY + 3);
      ctx.stroke();
      ctx.font = '500 10px "JetBrains Mono", monospace';
      ctx.letterSpacing = "0.14em";
      ctx.textBaseline = "middle";
      ctx.fillStyle = `rgba(${ink},0.55)`;
      ctx.fillText(`5 M · ROOM ${stage.room} × ${stage.room} M`, sx(-half) + 5 * scale + 10, barY);

      // Nodes ease toward where C++ says they are; overridden ones sit under the pointer.
      const targets = nodesOf(stage);
      for (const [key, target] of targets) {
        const goal = overrides.current.get(key) ?? target;
        const prev = drawn.get(key);
        const snap = !prev || overrides.current.has(key);
        drawn.set(key, snap ? { ...goal } : { x: prev.x + (goal.x - prev.x) * 0.25, y: prev.y + (goal.y - prev.y) * 0.25 });
      }
      for (const key of drawn.keys())
        if (!targets.has(key)) {
          drawn.delete(key);
          if (typeof key === "number") shown.delete(key);
        }

      // Selected node: a breathing ring.
      const sel = selected !== null ? drawn.get(selected) : undefined;
      if (sel) {
        const b = breath(t, 2.6);
        ctx.beginPath();
        ctx.arc(sx(sel.x), sy(sel.y), 11 + b * 5, 0, Math.PI * 2);
        ctx.strokeStyle = `rgba(${ink},${0.12 + 0.3 * (1 - b)})`;
        ctx.stroke();
      }

      // Input level: a soft disc behind each player (30 Hz from C++, eased per frame).
      for (const player of stage.players) {
        const p = drawn.get(player.id)!;
        const level = (shown.get(player.id) ?? 0) * 0.65 + levelOf(levels.current, player.id) * 0.35;
        shown.set(player.id, level);
        if (level < 0.01) continue;
        ctx.beginPath();
        ctx.arc(sx(p.x), sy(p.y), 6 + level * 14, 0, Math.PI * 2);
        ctx.fillStyle = `rgba(${ink},${0.04 + level * 0.12})`;
        ctx.fill();
      }

      // Names first, with a paper halo so they stay legible where players crowd (selected last);
      // nodes are drawn over them so a node is never hidden.
      ctx.lineJoin = "round";
      ctx.lineWidth = 3;
      ctx.strokeStyle = paper;
      const byPriority = [...stage.players].sort((a, b) => Number(a.id === selected) - Number(b.id === selected));
      for (const player of byPriority) {
        const p = drawn.get(player.id)!;
        const label = player.name.toUpperCase();
        ctx.strokeText(label, sx(p.x) + 12, sy(p.y));
        ctx.fillStyle = `rgba(${ink},${player.id === selected ? 0.85 : 0.55})`;
        ctx.fillText(label, sx(p.x) + 12, sy(p.y));
      }
      ctx.lineWidth = 1;

      // Listener: an arrowhead facing +y (front), turned with the head when tracking. Slim and
      // notched so its direction reads at any angle (an equilateral triangle turned 25°
      // looks like it points left).
      const l = drawn.get("listener")!;
      ctx.save();
      ctx.translate(sx(l.x), sy(l.y));
      ctx.rotate((headYaw.current * Math.PI) / 180);
      ctx.beginPath();
      ctx.moveTo(0, -8);
      ctx.lineTo(5.5, 6);
      ctx.lineTo(0, 3);
      ctx.lineTo(-5.5, 6);
      ctx.closePath();
      ctx.fillStyle = `rgba(${ink},0.9)`;
      ctx.fill();
      ctx.restore();

      for (const player of stage.players) {
        const p = drawn.get(player.id)!;
        const x = sx(p.x);
        const y = sy(p.y);
        ctx.beginPath();
        ctx.arc(x, y, 6, 0, Math.PI * 2);
        // The selected player (the one the side panel shows) is filled.
        if (player.id === selected) {
          ctx.fillStyle = `rgba(${ink},0.9)`;
          ctx.fill();
        } else {
          ctx.fillStyle = paper;
          ctx.fill();
          ctx.lineWidth = 1.5;
          ctx.strokeStyle = `rgba(${ink},0.75)`;
          ctx.stroke();
          ctx.lineWidth = 1;
        }
      }


      frame = requestAnimationFrame(draw);
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
    const { cx, cy, scale } = view.current;
    let best: NodeKey | null = null;
    let bestDistance = hitRadius;
    for (const [key, p] of nodesOf(stage)) {
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

  const onPointerDown = (e: PointerEvent<HTMLCanvasElement>) => {
    const key = hitTest(e);
    if (key === null) return;
    onSelect(key);
    e.currentTarget.setPointerCapture(e.pointerId);
    e.currentTarget.style.cursor = "grabbing";
    drag.current = { key, gesture: startGesture(key) };
  };

  const onPointerMove = (e: PointerEvent<HTMLCanvasElement>) => {
    if (drag.current) move(drag.current.key, drag.current.gesture, toMetres(e));
    else e.currentTarget.style.cursor = hitTest(e) !== null ? "grab" : "";
  };

  const onPointerUp = (e: PointerEvent<HTMLCanvasElement>) => {
    drag.current?.gesture.end();
    drag.current = null;
    e.currentTarget.style.cursor = hitTest(e) !== null ? "grab" : "";
  };

  // Arrow keys nudge the selected node 0.1 m (Shift: 1 m); one gesture per key press.
  const onKeyDown = (e: KeyboardEvent) => {
    const dir = nudgeKeys[e.key];
    if (!dir || selected === null) return;
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

  return (
    <canvas
      ref={canvasRef}
      className={styles.map}
      tabIndex={0}
      aria-label="Stage map. Arrow keys move the selected node, Shift for 1 m steps."
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
      onKeyDown={onKeyDown}
      onKeyUp={(e) => nudgeKeys[e.key] && endKeyGesture()}
      onBlur={endKeyGesture}
    />
  );
}
