import type { Point, Stage } from "./bridge";
import { levelOf, type Levels } from "./level";

export type NodeKey = "listener" | number;

// Where the room sits on the canvas: its centre in CSS pixels, and pixels per metre.
export type View = { cx: number; cy: number; scale: number };

export type MapFrame = {
  width: number; // CSS pixels
  height: number;
  seconds: number; // since the map appeared; drives the breathing ring
  stage: Stage;
  selected: NodeKey | null;
  overrides: ReadonlyMap<NodeKey, Point>; // nodes the user is moving, in metres
  levels: Levels;
  headYaw: number; // degrees clockwise
  ink: string; // "r,g,b"
  paper: string; // a CSS colour
};

const margin = 32; // px around the room square

type Text = { text: string; x: number; y: number; opacity: number; halo: boolean };

// Text drawn before a font has loaded is in a fallback font, so it is drawn again afterwards.
let fontLoads = 0;
document.fonts.addEventListener("loadingdone", () => fontLoads++);

// (1 - cos) / 2: eases at the extremes, so motion breathes rather than ticks.
const breath = (t: number, period: number) => (1 - Math.cos(((t % period) / period) * Math.PI * 2)) / 2;

export function nodesOf(stage: Stage): Map<NodeKey, Point> {
  const nodes = new Map<NodeKey, Point>([["listener", stage.listener]]);
  for (const p of stage.players) nodes.set(p.id, p);
  return nodes;
}

// Draws the stage map, one frame at a time. It remembers only what eases between frames:
// where each node is drawn and how high each level is shown.
export class StagePainter {
  private drawn = new Map<NodeKey, Point>(); // eased positions in metres
  private shown = new Map<number, number>(); // eased input levels, 0..1

  private copyKey = "";

  // `copy` holds the map's text as last drawn. Text is most of a frame's cost and changes
  // only when a player moves, is renamed or selected, so it is drawn once and copied afterwards.
  // It has to be a canvas in the page, next to the map: canvas text takes its smoothing from
  // the element's CSS, and one outside the page draws the same letters slightly differently.
  constructor(private copy: HTMLCanvasElement) {}

  paint(ctx: CanvasRenderingContext2D, frame: MapFrame): View {
    const { width: W, height: H, stage, selected, overrides, ink, paper } = frame;
    const { drawn, shown } = this;
    const half = stage.room / 2;
    const side = Math.max(0, Math.min(W, H) - margin * 2);
    const scale = side / stage.room;
    const cx = W / 2;
    const cy = H / 2;

    // Snap hairlines to the pixel grid so they stay crisp at 1x.
    const px = (v: number) => Math.round(v) + 0.5;
    const sx = (x: number) => cx + x * scale;
    const sy = (y: number) => cy - y * scale;

    ctx.clearRect(0, 0, W, H);
    ctx.lineWidth = 1;
    ctx.lineJoin = "round";

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
    const caption: Text = {
      text: `5 M · ROOM ${stage.room} × ${stage.room} M`,
      x: sx(-half) + 5 * scale + 10,
      y: barY,
      opacity: 0.55,
      halo: false,
    };

    // Nodes ease toward where C++ says they are; overridden ones sit under the pointer.
    const targets = nodesOf(stage);
    for (const [key, target] of targets) {
      const goal = overrides.get(key) ?? target;
      const prev = drawn.get(key);
      const snap = !prev || overrides.has(key);
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
      const b = breath(frame.seconds, 2.6);
      ctx.beginPath();
      ctx.arc(sx(sel.x), sy(sel.y), 11 + b * 5, 0, Math.PI * 2);
      ctx.strokeStyle = `rgba(${ink},${0.12 + 0.3 * (1 - b)})`;
      ctx.stroke();
    }

    // Input level: a soft disc behind each player (30 Hz from C++, eased per frame).
    for (const player of stage.players) {
      const p = drawn.get(player.id)!;
      const level = (shown.get(player.id) ?? 0) * 0.65 + levelOf(frame.levels, player.id) * 0.35;
      shown.set(player.id, level);
      if (level < 0.01) continue;
      ctx.beginPath();
      ctx.arc(sx(p.x), sy(p.y), 6 + level * 14, 0, Math.PI * 2);
      ctx.fillStyle = `rgba(${ink},${0.04 + level * 0.12})`;
      ctx.fill();
    }

    // Names with a paper halo, so they stay legible where players crowd (selected last).
    // They go over the level discs and under the nodes, so a node is never hidden.
    const byPriority = [...stage.players].sort((a, b) => Number(a.id === selected) - Number(b.id === selected));
    const names = byPriority.map((player): Text => {
      const p = drawn.get(player.id)!;
      return { text: player.name.toUpperCase(), x: sx(p.x) + 12, y: sy(p.y), opacity: player.id === selected ? 0.85 : 0.55, halo: true };
    });
    this.paintText(ctx, [caption, ...names], ink, paper);

    // Listener: an arrowhead facing +y (front), turned with the head when tracking. Slim and
    // notched so its direction reads at any angle (an equilateral triangle turned 25°
    // looks like it points left).
    const l = drawn.get("listener")!;
    ctx.save();
    ctx.translate(sx(l.x), sy(l.y));
    ctx.rotate((frame.headYaw * Math.PI) / 180);
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
      ctx.beginPath();
      ctx.arc(sx(p.x), sy(p.y), 6, 0, Math.PI * 2);
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

    return { cx, cy, scale };
  }

  // Copies the map's text from `copy`, drawing it there first if any of it changed.
  private paintText(ctx: CanvasRenderingContext2D, texts: Text[], ink: string, paper: string) {
    const { copy } = this;
    const pixels = ctx.canvas;

    // Everything the picture depends on; positions to 1/64 px, finer than can be seen.
    const key = JSON.stringify([
      pixels.width,
      pixels.height,
      ctx.getTransform().a, // the pixel ratio
      fontLoads,
      ink,
      paper,
      texts.map((t) => [t.text, Math.round(t.x * 64), Math.round(t.y * 64), t.opacity]),
    ]);

    if (key !== this.copyKey) {
      this.copyKey = key;
      copy.width = pixels.width; // also clears it
      copy.height = pixels.height;
      const to = copy.getContext("2d")!;
      to.setTransform(ctx.getTransform());
      to.font = '500 10px "JetBrains Mono", monospace';
      to.letterSpacing = "0.14em";
      to.textBaseline = "middle";
      to.lineJoin = "round";
      to.lineWidth = 3;
      to.strokeStyle = paper;
      for (const t of texts) {
        if (t.halo) to.strokeText(t.text, t.x, t.y);
        to.fillStyle = `rgba(${ink},${t.opacity})`;
        to.fillText(t.text, t.x, t.y);
      }
    }

    if (copy.width === 0 || copy.height === 0) return;
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0); // pixel for pixel, whatever the pixel ratio
    ctx.drawImage(copy, 0, 0);
    ctx.restore();
  }
}
