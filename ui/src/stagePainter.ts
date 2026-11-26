import type { Point, Stage } from "./bridge";
import { easeToward } from "./ease";
import { drawIcon, playerIcon } from "./icons";
import type { Levels } from "./level";
import { seatLevel, type SeatedStage } from "./players";

export type NodeKey = "listener" | number;
export type MapView = "grid" | "azimuth";
export type MapNames = "selected" | "all";

// Where the room sits in CSS pixels, and where its markers were drawn in metres.
export type View = { cx: number; cy: number; scale: number; drawn: ReadonlyMap<NodeKey, Point> };

export type MapFrame = {
  width: number; // CSS pixels
  height: number;
  seconds: number; // since the map appeared; drives the breathing ring
  stage: SeatedStage;
  selected: NodeKey | null;
  overrides: ReadonlyMap<NodeKey, Point>; // nodes the user is moving, in metres
  levels: Levels;
  headYaw: number; // degrees clockwise
  view: MapView;
  names: MapNames;
  ink: string; // "r,g,b"
  paper: string; // a CSS colour
};

// px around the room square: room for the view switch above it, the scale bar below,
// and the azimuth labels around it.
const margin = { top: 56, side: 32, bottom: 40 };
export const badgeRadius = 14; // px

type Text = { text: string; x: number; y: number; opacity: number; halo: boolean; align: CanvasTextAlign };

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
// where each node is drawn, how high each level is shown and how far the azimuth view is in.
export class StagePainter {
  private drawn = new Map<NodeKey, Point>(); // eased positions in metres
  private shown = new Map<number, number>(); // eased input levels, 0..1
  private rose = -1; // 0 grid .. 1 azimuth, eased; -1 until the first frame

  private copyKey = "";
  private lastSeconds: number | undefined;

  // Every node is where C++ says it is: nothing is easing, so the map may be drawn less often.
  settled = false;

  // `copy` holds the map's text as last drawn. Text is most of a frame's cost and changes
  // only when a player moves, is renamed or selected, so it is drawn once and copied afterwards.
  // It has to be a canvas in the page, next to the map: canvas text takes its smoothing from
  // the element's CSS, and one outside the page draws the same letters slightly differently.
  constructor(private copy: HTMLCanvasElement) {}

  paint(ctx: CanvasRenderingContext2D, frame: MapFrame): View {
    const { width: W, height: H, stage, selected, overrides, ink, paper } = frame;
    const { drawn, shown } = this;
    const dt = Math.min(0.1, Math.max(0, frame.seconds - (this.lastSeconds ?? frame.seconds - 1 / 60)));
    this.lastSeconds = frame.seconds;
    this.settled = true;
    const half = stage.room / 2;
    const tall = H - margin.top - margin.bottom;
    const side = Math.max(0, Math.min(W - margin.side * 2, tall));
    const scale = side / stage.room;
    const cx = W / 2;
    const cy = margin.top + tall / 2;
    const TAU = Math.PI * 2;

    // Snap hairlines to the pixel grid so they stay crisp at 1x.
    const px = (v: number) => Math.round(v) + 0.5;
    const sx = (x: number) => cx + x * scale;
    const sy = (y: number) => cy - y * scale;

    // Grid <-> azimuth: a quick cross-fade, not a cut.
    const roseGoal = frame.view === "azimuth" ? 1 : 0;
    const rose = this.rose < 0 ? roseGoal : easeToward(this.rose, roseGoal, 0.2, dt);
    this.rose = Math.abs(rose - roseGoal) < 0.002 ? roseGoal : rose;
    if (this.rose !== roseGoal) this.settled = false;

    ctx.clearRect(0, 0, W, H);
    ctx.lineWidth = 1;
    ctx.lineJoin = "round";

    // Grid: 1 m minor (skipped when too dense, gone in the azimuth view), 5 m major,
    // counted from the centre.
    for (let m = -Math.floor(half); m <= half; m++) {
      if (Math.abs(m) >= half) continue; // the outline covers the edges
      const major = m % 5 === 0;
      if (!major && (scale < 6 || this.rose > 0.999)) continue;
      ctx.strokeStyle = `rgba(${ink},${major ? 0.1 : 0.05 * (1 - this.rose)})`;
      ctx.beginPath();
      ctx.moveTo(px(sx(m)), sy(half));
      ctx.lineTo(px(sx(m)), sy(-half));
      ctx.moveTo(sx(-half), px(sy(m)));
      ctx.lineTo(sx(half), px(sy(m)));
      ctx.stroke();
    }

    ctx.strokeStyle = `rgba(${ink},0.2)`;
    ctx.strokeRect(px(sx(-half)), px(sy(half)), px(sx(half)) - px(sx(-half)), px(sy(-half)) - px(sy(half)));

    const texts: Text[] = [];

    // Azimuth view: a circle inscribed in the room, ticks every 5 and 15 degrees (inwards),
    // labels every 30. Angles as the DSP counts them: 0 ahead, +90 right.
    if (this.rose > 0.001) {
      const a = this.rose;
      const R = half * scale;
      const at = (deg: number, r: number) => [cx + Math.sin((deg * Math.PI) / 180) * r, cy - Math.cos((deg * Math.PI) / 180) * r];
      ctx.strokeStyle = `rgba(${ink},${0.2 * a})`;
      ctx.beginPath();
      ctx.arc(cx, cy, R, 0, TAU);
      ctx.stroke();
      ctx.strokeStyle = `rgba(${ink},${0.07 * a})`;
      ctx.beginPath();
      for (let deg = 30; deg < 180; deg += 30) {
        const [x0, y0] = at(deg, R);
        const [x1, y1] = at(deg + 180, R);
        ctx.moveTo(x0, y0);
        ctx.lineTo(x1, y1);
      }
      ctx.stroke();
      ctx.strokeStyle = `rgba(${ink},${0.14 * a})`;
      ctx.beginPath();
      ctx.moveTo(px(cx), cy - R);
      ctx.lineTo(px(cx), cy + R);
      ctx.stroke();
      for (const [step, len, alpha] of [[5, 0.28, 0.25], [15, 0.55, 0.45]] as const) {
        ctx.strokeStyle = `rgba(${ink},${alpha * a})`;
        ctx.beginPath();
        for (let deg = 0; deg < 360; deg += step) {
          if (step === 5 && deg % 15 === 0) continue;
          const [x0, y0] = at(deg, R);
          const [x1, y1] = at(deg, R - len * scale);
          ctx.moveTo(x0, y0);
          ctx.lineTo(x1, y1);
        }
        ctx.stroke();
      }
      for (let deg = -150; deg <= 180; deg += 30) {
        const [x, y] = at(deg, R + 14); // outside the circle, clear of the players
        texts.push({ text: deg < 0 ? `−${-deg}°` : `${deg}°`, x, y, opacity: (deg === 0 ? 0.8 : 0.5) * a, halo: false, align: "center" });
      }
    }

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
    texts.push({ text: "5 M", x: sx(-half) + 5 * scale + 10, y: barY, opacity: 0.55, halo: false, align: "left" });

    // Nodes ease toward where C++ says they are; overridden ones sit under the pointer.
    const targets = nodesOf(stage);
    for (const [key, target] of targets) {
      const goal = overrides.get(key) ?? target;
      const prev = drawn.get(key);
      const snap = !prev || overrides.has(key);
      const next = snap ? { ...goal } : { x: easeToward(prev.x, goal.x, 0.25, dt), y: easeToward(prev.y, goal.y, 0.25, dt) };
      // Close enough that the eye can't tell (1 mm): arrive, so the map can rest.
      if (Math.abs(next.x - goal.x) < 0.001 && Math.abs(next.y - goal.y) < 0.001) Object.assign(next, goal);
      else this.settled = false;
      drawn.set(key, next);
    }
    for (const key of drawn.keys())
      if (!targets.has(key)) {
        drawn.delete(key);
        if (typeof key === "number") shown.delete(key);
      }

    // Azimuth view: a dashed line from the listener to the selected player.
    const listener = drawn.get("listener")!;
    const sel = selected !== null ? drawn.get(selected) : undefined;
    if (this.rose > 0.001 && sel && selected !== "listener") {
      ctx.save();
      ctx.setLineDash([2, 3]);
      ctx.strokeStyle = `rgba(${ink},${0.35 * this.rose})`;
      ctx.beginPath();
      ctx.moveTo(sx(listener.x), sy(listener.y));
      ctx.lineTo(sx(sel.x), sy(sel.y));
      ctx.stroke();
      ctx.restore();
    }

    // Selected node: a breathing ring.
    if (sel) {
      const b = breath(frame.seconds, 2.6);
      const r = selected === "listener" ? 11 : badgeRadius;
      ctx.beginPath();
      ctx.arc(sx(sel.x), sy(sel.y), r * (1.3 + 0.35 * b), 0, TAU);
      ctx.strokeStyle = `rgba(${ink},${0.12 + 0.3 * (1 - b)})`;
      ctx.stroke();
    }

    // Input level: a soft halo around each badge (30 Hz from C++, eased per frame).
    for (const player of stage.players) {
      const p = drawn.get(player.id)!;
      const level = easeToward(shown.get(player.id) ?? 0, seatLevel(frame.levels, player.tracks.map((t) => t.id)), 0.35, dt);
      shown.set(player.id, level);
      if (level < 0.01) continue;
      ctx.beginPath();
      ctx.arc(sx(p.x), sy(p.y), badgeRadius + level * 10, 0, TAU);
      ctx.fillStyle = `rgba(${ink},${0.04 + level * 0.12})`;
      ctx.fill();
    }

    // Names under the badges, with a paper halo so they stay legible where players crowd.
    // Only the selected one by default; drawn before the badges, so no badge is ever hidden.
    const byPriority = [...stage.players].sort((a, b) => Number(a.id === selected) - Number(b.id === selected));
    for (const player of byPriority) {
      if (frame.names !== "all" && player.id !== selected) continue;
      const p = drawn.get(player.id)!;
      texts.push({ text: player.name.toUpperCase(), x: sx(p.x), y: sy(p.y) + badgeRadius + 10, opacity: player.id === selected ? 0.85 : 0.55, halo: true, align: "center" });
    }
    this.paintText(ctx, texts, ink, paper);

    // Listener: an arrowhead facing +y (front), turned with the head when tracking. Slim and
    // notched so its direction reads at any angle (an equilateral triangle turned 25°
    // looks like it points left).
    ctx.save();
    ctx.translate(sx(listener.x), sy(listener.y));
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

    // Players: a badge with the instrument's icon; the selected one (the one the panel shows) is filled.
    for (const player of byPriority) {
      const p = drawn.get(player.id)!;
      const x = sx(p.x);
      const y = sy(p.y);
      const chosen = player.id === selected;
      ctx.beginPath();
      ctx.arc(x, y, badgeRadius, 0, TAU);
      if (chosen) {
        ctx.fillStyle = `rgba(${ink},0.9)`;
        ctx.fill();
      } else {
        ctx.fillStyle = paper;
        ctx.fill();
        ctx.strokeStyle = `rgba(${ink},0.35)`;
        ctx.stroke();
      }
      drawIcon(ctx, playerIcon(player), x, y, badgeRadius * 1.3, chosen ? paper : `rgb(${ink})`);

      // A player with several tracks: how many, on a tab at the badge's shoulder.
      if (player.tracks.length > 1) {
        const tx = x + badgeRadius * 0.78;
        const ty = y - badgeRadius * 0.78;
        ctx.save();
        ctx.beginPath();
        ctx.arc(tx, ty, 6.5, 0, TAU);
        ctx.fillStyle = chosen ? paper : `rgb(${ink})`;
        ctx.fill();
        if (chosen) {
          ctx.strokeStyle = `rgba(${ink},0.9)`;
          ctx.stroke();
        }
        ctx.font = '600 8px "JetBrains Mono", monospace';
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillStyle = chosen ? `rgb(${ink})` : paper;
        ctx.fillText(String(player.tracks.length), tx, ty + 0.5);
        ctx.restore();
      }
    }

    return { cx, cy, scale, drawn };
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
      texts.map((t) => [t.text, Math.round(t.x * 64), Math.round(t.y * 64), Math.round(t.opacity * 100), t.align]),
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
        to.textAlign = t.align;
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
