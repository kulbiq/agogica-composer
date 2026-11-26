import { seatPitch, seatsIn, type Audience } from "./audience";
import type { Point } from "./bridge";
import { easeToward } from "./ease";
import type { Zoom } from "./mapEdits";
import { drawIcon, playerIcon } from "./icons";
import type { Levels } from "./level";
import { seatLevel, type Seat, type SeatedStage } from "./players";
import { swoop, view3d, type Pt } from "./room/camera";
import { drawFigure } from "./room/figures";
import { drawDistanceArcs, drawRoom, drawSeat, drawVignette, layoutRoom, type Layout, type Materials } from "./room/scene";

export type NodeKey = "listener" | number;
export type MapView = "grid" | "azimuth";
export type MapNames = "selected" | "all";

// A node on screen that can be clicked.
export type Hit = { key: NodeKey; x: number; y: number; r: number };
// Where the frame was drawn: the flat map's pixels per metre around (cx, cy), valid while
// `flat`; and the clickable nodes, nearest first.
export type View = { cx: number; cy: number; scale: number; flat: boolean; hits: Hit[] };

// The 3D view: on or off (the camera swoops between), which way the eyes look, seated or standing;
// `at`: where a spectator stands (the camera walks without the listener), else at the listener.
export type Look = { on: boolean; yaw: number; pitch: number; standing: boolean; at?: Point };

export type MapFrame = {
  width: number; // CSS pixels
  height: number;
  seconds: number; // since the map appeared; drives the breathing ring and the eases
  stage: SeatedStage;
  selected: NodeKey | null; // the one that leads (Edit player, the azimuth line)
  selection: readonly NodeKey[]; // every selected node, the leading one included
  zoom: Zoom; // the map's (the room's view starts from it)
  audience: Audience; // where the audience sits
  painting: boolean; // the audience is being painted: its seats show stronger on the map
  top: number; // px clear above the room on the map (the tools over it)
  overrides: ReadonlyMap<NodeKey, Point>; // nodes the user is moving, in metres
  levels: Levels;
  facing: number; // degrees clockwise the listener faces, as heard (the map's arrow)
  look: Look;
  view: MapView;
  names: MapNames;
  ink: string; // "r,g,b"
  paper: string; // a CSS colour
  materials: Materials;
  reducedMotion: boolean;
};

// px around the room square: room for the view switch above it, the scale bar below,
// and the azimuth labels around it.
const margin = { top: 56, side: 32, bottom: 40 };
export const badgeRadius = 14; // px, on the map
const hitRadius = badgeRadius + 2;
const TAU = Math.PI * 2;

type Text = { text: string; x: number; y: number; opacity: number; halo: boolean; align: CanvasTextAlign };

// Text drawn before a font has loaded is in a fallback font, so it is drawn again afterwards.
let fontLoads = 0;
document.fonts.addEventListener("loadingdone", () => fontLoads++);

// (1 - cos) / 2: eases at the extremes, so motion breathes rather than ticks.
const breath = (t: number, period: number) => (1 - Math.cos(((t % period) / period) * TAU)) / 2;
const smoothstep = (a: number, b: number, x: number) => {
  const u = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return u * u * (3 - 2 * u);
};

// A timed (1 - cos) / 2 ease between two values, restarted from wherever it is.
class Glide {
  private from = 0;
  private to = 0;
  private start = -Infinity;
  value = 0;
  moving = false;

  constructor(private seconds: number) {}

  step(goal: number, now: number, instant: boolean) {
    if (this.start === -Infinity || instant) {
      this.from = this.to = this.value = goal;
      this.start = now;
    } else if (goal !== this.to) {
      this.from = this.value;
      this.to = goal;
      this.start = now;
    }
    const p = Math.min(1, (now - this.start) / this.seconds);
    this.value = this.from + ((this.to - this.from) * (1 - Math.cos(Math.PI * p))) / 2;
    this.moving = this.from !== this.to && p < 1;
    return this.value;
  }
}

export function nodesOf(stage: SeatedStage): Map<NodeKey, Point> {
  const nodes = new Map<NodeKey, Point>([["listener", stage.listener]]);
  for (const p of stage.players) nodes.set(p.id, p);
  return nodes;
}

// Draws the stage, one frame at a time: the map from above, the 3D room at the listener's eyes,
// and the swoop between them, all through one camera. It remembers only what eases between
// frames: where each node is drawn, how high each level is shown, how far the azimuth view and
// the 3D view are in, the eye height.
export class StagePainter {
  private drawn = new Map<NodeKey, Point>(); // eased positions in metres
  private shown = new Map<number, number>(); // eased input levels, 0..1
  private rose = -1; // 0 grid .. 1 azimuth, eased; -1 until the first frame
  private amount = new Glide(view3d.swoopMs / 1000); // 0 map .. 1 eyes
  private eyes = new Glide(0.7);
  private layout: Layout | undefined;

  private copyKey = "";
  private lastSeconds: number | undefined;

  // Every node is where C++ says it is and no view is changing: the map may be drawn less often.
  settled = false;

  // The camera is down at the listener's eyes (walking starts there).
  get inRoom() {
    return this.amount.value > 0.999;
  }

  // `copy` holds the map's text as last drawn. Text is most of a flat frame's cost and changes
  // only when a player moves, is renamed or selected, so it is drawn once and copied afterwards.
  // It has to be a canvas in the page, next to the map: canvas text takes its smoothing from
  // the element's CSS, and one outside the page draws the same letters slightly differently.
  constructor(private copy: HTMLCanvasElement) {}

  paint(ctx: CanvasRenderingContext2D, frame: MapFrame): View {
    const { width: W, height: H, stage, selected, overrides, ink, paper, look, materials: M, zoom } = frame;
    const chosen = (key: NodeKey) => frame.selection.includes(key);
    const { drawn, shown } = this;
    const dt = Math.min(0.1, Math.max(0, frame.seconds - (this.lastSeconds ?? frame.seconds - 1 / 60)));
    this.lastSeconds = frame.seconds;
    this.settled = true;
    const half = stage.room / 2;
    const top = Math.max(margin.top, frame.top);
    const tall = H - top - margin.bottom;
    const side = Math.max(0, Math.min(W - margin.side * 2, tall));
    // The whole room fits (zoom 1), around the zoom's centre.
    const scale = (side / stage.room) * zoom.k;
    const cx = W / 2 - zoom.x * scale;
    const cy = top + tall / 2 + zoom.y * scale;

    // Grid <-> azimuth: a quick cross-fade, not a cut.
    const roseGoal = frame.view === "azimuth" ? 1 : 0;
    const rose = this.rose < 0 ? roseGoal : easeToward(this.rose, roseGoal, 0.2, dt);
    this.rose = Math.abs(rose - roseGoal) < 0.002 ? roseGoal : rose;
    if (this.rose !== roseGoal) this.settled = false;

    // Map <-> 3D: the camera swoops down to the listener's eyes (1.7 s); seated or standing.
    const e = this.amount.step(look.on ? 1 : 0, frame.seconds, frame.reducedMotion);
    const eyes = this.eyes.step(look.standing ? view3d.standingEyes : view3d.seatedEyes, frame.seconds, frame.reducedMotion);
    if (this.amount.moving || this.eyes.moving) this.settled = false;

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
    for (const player of stage.players)
      shown.set(player.id, easeToward(shown.get(player.id) ?? 0, seatLevel(frame.levels, player.tracks.map((t) => t.id)), 0.35, dt));

    const listener = drawn.get("listener")!;
    const viewer = look.at ?? listener; // where the eyes are in the room
    // In the room, straight ahead is the middle of the view below the tools (a second row of
    // them, in a narrow window, would cover the nearest names).
    const below = H + top - margin.top;
    const cam = swoop({ amount: e, width: W, height: below, map: { x: cx, y: cy, scale }, listener: viewer, eyes, yaw: look.yaw, pitch: look.pitch });
    const flat = cam.flat;
    const shows = smoothstep(0.15, 0.75, e); // how much of the room's materials show
    const fog = (d: number) => Math.min(0.72, Math.max(0, (d - 3) / 24)) * shows;
    // Snap hairlines to the pixel grid so they stay crisp on the flat map.
    // (1e-6: the camera's float error must not tip a line that falls on a half pixel.)
    const px = (v: number) => Math.round(v + 1e-6) + 0.5;
    const snap = flat ? px : undefined;
    const line = (a: Pt, b: Pt) => cam.seg(ctx, a, b, snap);

    ctx.clearRect(0, 0, W, H);
    ctx.lineWidth = 1;
    ctx.lineJoin = "round";

    if (shows > 0.002) {
      if (this.layout?.room !== stage.room) this.layout = layoutRoom(stage.room);
      drawRoom(ctx, cam, this.layout, M, shows, fog);
    }

    // Grid: 1 m minor (skipped when too dense, gone in the azimuth view), 5 m major, counted
    // from the centre, and the room's outline. In the room it would read as tiles, halving
    // every distance, so it fades out there.
    const grid = 1 - shows;
    ctx.lineWidth = 1;
    if (grid > 0.002) {
      for (const major of [false, true]) {
        if (!major && (scale < 6 || this.rose > 0.999)) continue;
        ctx.beginPath();
        for (let m = -Math.floor(half); m <= half; m++) {
          if (Math.abs(m) >= half || (m % 5 === 0) !== major) continue; // the outline covers the edges
          line([m, half, 0], [m, -half, 0]);
          line([-half, m, 0], [half, m, 0]);
        }
        ctx.strokeStyle = `rgba(${ink},${(major ? 0.1 : 0.05 * (1 - this.rose)) * grid})`;
        ctx.stroke();
      }
      ctx.beginPath();
      line([-half, half, 0], [half, half, 0]);
      line([half, half, 0], [half, -half, 0]);
      line([half, -half, 0], [-half, -half, 0]);
      line([-half, -half, 0], [-half, half, 0]);
      ctx.strokeStyle = `rgba(${ink},${0.2 * grid})`;
      ctx.stroke();
    }

    const texts: Text[] = [];

    // Azimuth view: a circle inscribed in the room, ticks every 5 and 15 degrees (inwards),
    // labels every 30 just outside. Angles as the DSP counts them: 0 ahead, +90 right.
    const roseShows = this.rose * (1 - 0.6 * shows);
    if (roseShows > 0.001) {
      const R = half;
      const at = (deg: number, r: number): Pt => [Math.sin((deg * Math.PI) / 180) * r, Math.cos((deg * Math.PI) / 180) * r, 0];
      ctx.strokeStyle = `rgba(${ink},${0.2 * roseShows})`;
      ctx.beginPath();
      for (let deg = 0; deg < 360; deg += 2.5) cam.seg(ctx, at(deg, R), at(deg + 2.5, R));
      ctx.stroke();
      ctx.strokeStyle = `rgba(${ink},${0.07 * roseShows})`;
      ctx.beginPath();
      for (let deg = 30; deg < 180; deg += 30) cam.seg(ctx, at(deg, R), at(deg + 180, R));
      ctx.stroke();
      ctx.strokeStyle = `rgba(${ink},${0.14 * roseShows})`;
      ctx.beginPath();
      line(at(0, R), at(180, R));
      ctx.stroke();
      for (const [step, len, alpha] of [[5, 0.28, 0.25], [15, 0.55, 0.45]] as const) {
        ctx.strokeStyle = `rgba(${ink},${alpha * roseShows})`;
        ctx.beginPath();
        for (let deg = 0; deg < 360; deg += step) {
          if (step === 5 && deg % 15 === 0) continue;
          cam.seg(ctx, at(deg, R), at(deg, R - len));
        }
        ctx.stroke();
      }
      const labels = roseShows * (1 - e);
      if (labels > 0.01)
        for (let deg = -150; deg <= 180; deg += 30) {
          const p = cam.screen(at(deg, R + 14 / scale), 0.1); // outside the circle, clear of the players
          if (p) texts.push({ text: deg < 0 ? `−${-deg}°` : `${deg}°`, x: p[0], y: p[1], opacity: (deg === 0 ? 0.8 : 0.5) * labels, halo: false, align: "center" });
        }
    }

    // Scale bar: 5 m (less when zoomed in), under the room's bottom-left corner, or the view's
    // when the corner is out of view (the map only).
    if (e < 0.98) {
      const metres = zoom.k > 1 ? ([5, 2, 1, 0.5].find((m) => m * scale <= 240) ?? 0.5) : 5;
      const left = Math.max(-half, (16 - cx) / scale);
      const below = Math.max(-half - 16 / scale, -(H - 16 - cy) / scale);
      const a = cam.screen([left, below, 0], 0.1);
      const b = cam.screen([left + metres, below, 0], 0.1);
      if (a && b) {
        const y = px(a[1]);
        ctx.globalAlpha = 1 - e;
        ctx.strokeStyle = `rgba(${ink},0.45)`;
        ctx.beginPath();
        ctx.moveTo(a[0], y);
        ctx.lineTo(b[0], y);
        ctx.moveTo(px(a[0]), y - 3);
        ctx.lineTo(px(a[0]), y + 3);
        ctx.moveTo(px(b[0]), y - 3);
        ctx.lineTo(px(b[0]), y + 3);
        ctx.stroke();
        ctx.globalAlpha = 1;
        texts.push({ text: `${metres} M`, x: b[0] + 10, y, opacity: 0.55 * (1 - e), halo: false, align: "left" });
      }
    }

    // In the room: where 3, 5 and 10 m really are, and a soft vignette.
    if (shows > 0.002) {
      drawDistanceArcs(ctx, cam, viewer, look.yaw, ink, shows, (text, x, y) => this.label(ctx, text, x, y, 0.7, ink, paper));
      drawVignette(ctx, W, H, M, shows);
    }

    // Azimuth view: a dashed line from the listener to the selected player.
    const sel = selected !== null ? drawn.get(selected) : undefined;
    const dash = this.rose * (1 - e);
    if (dash > 0.001 && sel && selected !== "listener") {
      ctx.save();
      ctx.setLineDash([2, 3]);
      ctx.strokeStyle = `rgba(${ink},${0.35 * dash})`;
      ctx.beginPath();
      cam.seg(ctx, [listener.x, listener.y, 0], [sel.x, sel.y, 0]);
      ctx.stroke();
      ctx.restore();
    }

    // Shadows on the floor under the players.
    if (shows > 0.002) {
      ctx.globalAlpha = shows;
      ctx.fillStyle = `rgba(${M.shade},${M.shadow})`;
      for (const player of stage.players) {
        const p = drawn.get(player.id)!;
        const circle: Pt[] = [];
        for (let i = 0; i < 16; i++) circle.push([p.x + Math.cos((i / 16) * TAU) * 0.38, p.y + Math.sin((i / 16) * TAU) * 0.38, 0]);
        ctx.beginPath();
        if (cam.poly(ctx, circle)) ctx.fill();
      }
      ctx.globalAlpha = 1;
    }

    // The audience on the map: a square for each seat (stronger while it is being painted).
    const audience = seatsIn(frame.audience, stage.room);
    if (e < 0.5) {
      ctx.fillStyle = `rgba(${ink},${(frame.painting ? 0.16 : 0.07) * (1 - 2 * e)})`;
      const s = seatPitch * 0.35;
      for (const p of audience) {
        ctx.beginPath();
        if (cam.poly(ctx, [[p.x - s, p.y - s, 0], [p.x + s, p.y - s, 0], [p.x + s, p.y + s, 0], [p.x - s, p.y + s, 0]])) ctx.fill();
      }
    }

    // Audience seats (in the room only), clear of the players and the listener.
    const seats = (shows > 0.002 ? audience : [])
      .filter((s) => Math.hypot(s.x - viewer.x, s.y - viewer.y) > 0.5 && stage.players.every((p) => Math.hypot(s.x - p.x, s.y - p.y) > 0.75))
      .filter((s) => Math.hypot(s.x - cam.x, s.y - cam.y) < 24)
      .map((s) => ({ s, z: cam.toCam(s.x, s.y, 0.45).z }))
      .filter((o) => o.z > 0.25)
      .sort((a, b) => b.z - a.z);

    const hits: Hit[] = [];
    const byPriority = [...stage.players].sort((a, b) => Number(chosen(a.id)) - Number(chosen(b.id)) || Number(a.id === selected) - Number(b.id === selected));

    if (flat) {
      // The map: the selected node's ring, levels, names, the listener, the badges, in that
      // order, so no badge is ever hidden.
      const at = (p: Point) => cam.screen([p.x, p.y, 0])!;
      for (const key of frame.selection) {
        const p = drawn.get(key);
        if (!p) continue;
        const [x, y] = at(p);
        this.ring(ctx, x, y, key === "listener" ? 11 : badgeRadius, frame.seconds, ink);
      }
      for (const player of stage.players) {
        const [x, y] = at(drawn.get(player.id)!);
        this.halo(ctx, x, y, badgeRadius, shown.get(player.id) ?? 0, ink);
      }
      for (const player of byPriority) {
        if (frame.names !== "all" && !chosen(player.id)) continue;
        const [x, y] = at(drawn.get(player.id)!);
        texts.push({ text: player.name.toUpperCase(), x, y: y + badgeRadius + 10, opacity: chosen(player.id) ? 0.85 : 0.55, halo: true, align: "center" });
      }
      this.paintText(ctx, texts, ink, paper);
      const [lx, ly] = at(listener);
      this.arrow(ctx, lx, ly, frame.facing, 1, ink);
      hits.push({ key: "listener", x: lx, y: ly, r: hitRadius });
      for (const player of byPriority) {
        const [x, y] = at(drawn.get(player.id)!);
        this.badge(ctx, player, x, y, badgeRadius, chosen(player.id), ink, paper);
        hits.push({ key: player.id, x, y, r: hitRadius });
      }
      return { cx, cy, scale, flat, hits };
    }

    // The swoop and the room: everything by depth, far to near, so nearer covers farther.
    // Badges rise to pins just over the players' heads (the top of a seated head is at 1.37 m)
    // and grow with the room's scale (to 48 px); a name sits over its badge.
    for (const t of texts) this.label(ctx, t.text, t.x, t.y, t.opacity, ink, paper, t.align, t.halo);
    this.copyKey = ""; // the cached text is stale now
    const z = 1.45 * e;
    const allNames = frame.names === "all" || e > 0.5;
    const items = stage.players
      .map((player) => {
        const p = drawn.get(player.id)!;
        const c = cam.toCam(p.x, p.y, z);
        return { player, p, c, chosen: chosen(player.id) };
      })
      .filter((o) => o.c.z > 0.25)
      .sort((a, b) => b.c.z - a.c.z)
      .map((o, order) => {
        const [x, top] = cam.project(o.c);
        const rb = Math.min(48, badgeRadius + (Math.max(6, cam.scaleAt(o.c) * 0.17) - badgeRadius) * e);
        const y = top - (rb + 4) * e; // the pin's foot on the head, the badge above it
        let name: Text | undefined;
        if (o.chosen || allNames) {
          const fade = e > 0.5 ? Math.max(0.3, 1 - (o.c.z - 4) / 18) : 1;
          const opacity = (o.chosen ? 0.85 : 0.55) * fade;
          name = { text: o.player.name.toUpperCase(), x, y: e > 0.5 ? y - rb - 9 : y + rb + 10, opacity, halo: true, align: "center" };
        }
        return { ...o, x, y, rb, name, order };
      });

    // Which names show: the selected one first, then nearest first; one that would cover a name
    // already placed is left out (crowded players would make all of them unreadable). Each is
    // drawn with its player, so a nearer player covers a farther name.
    ctx.font = '500 10px "JetBrains Mono", monospace';
    ctx.letterSpacing = "0.14em";
    const placed: { x0: number; x1: number; y0: number; y1: number }[] = [];
    for (const o of [...items].sort((a, b) => Number(b.chosen) - Number(a.chosen) || a.c.z - b.c.z || b.order - a.order)) {
      const n = o.name;
      if (!n) continue;
      const w = ctx.measureText(n.text).width / 2 + 4;
      const box = { x0: n.x - w, x1: n.x + w, y0: n.y - 8, y1: n.y + 8 };
      if (placed.some((b) => box.x0 < b.x1 && b.x0 < box.x1 && box.y0 < b.y1 && b.y0 < box.y1)) o.name = undefined;
      else placed.push(box);
    }

    let nextSeat = 0;
    const seatsTo = (depth: number) => {
      while (nextSeat < seats.length && seats[nextSeat].z >= depth) drawSeat(ctx, cam, seats[nextSeat++].s, M, shows, fog);
    };
    for (const { player, p, c, chosen, x, y, rb, name } of items) {
      seatsTo(c.z);
      if (shows > 0.002) drawFigure(ctx, cam, p, chosen, shows, ink, fog);
      this.halo(ctx, x, y, rb, shown.get(player.id) ?? 0, ink);
      if (chosen) this.ring(ctx, x, y, rb, frame.seconds, ink);
      this.badge(ctx, player, x, y, rb, chosen, ink, paper);
      if (name) this.label(ctx, name.text, name.x, name.y, name.opacity, ink, paper);
      hits.unshift({ key: player.id, x, y, r: rb + 2 }); // nearest first
    }
    seatsTo(-Infinity);

    // The listener's arrow, until you are the listener (a spectator keeps seeing it).
    if (e < 0.999 || look.at) {
      const p = cam.screen([listener.x, listener.y, 0], 0.1);
      if (p) {
        this.arrow(ctx, p[0], p[1], frame.facing - cam.shot.yaw, look.at ? 1 : 1 - e, ink);
        if (e < 0.5) hits.push({ key: "listener", x: p[0], y: p[1], r: hitRadius });
      }
    }

    // Walking: a minimap with where you are and where you look.
    const mini = smoothstep(0.6, 1, e);
    if (mini > 0.002) this.minimap(ctx, W, stage, drawn, selected, viewer, look.yaw, look.at ? listener : undefined, eyes, mini, ink, paper);

    return { cx, cy, scale, flat, hits };
  }

  private ring(ctx: CanvasRenderingContext2D, x: number, y: number, r: number, seconds: number, ink: string) {
    const b = breath(seconds, 2.6);
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(x, y, r * (1.3 + 0.35 * b), 0, TAU);
    ctx.strokeStyle = `rgba(${ink},${0.12 + 0.3 * (1 - b)})`;
    ctx.stroke();
  }

  // Input level: a soft halo around a badge (30 Hz from C++, eased per frame).
  private halo(ctx: CanvasRenderingContext2D, x: number, y: number, r: number, level: number, ink: string) {
    if (level < 0.01) return;
    ctx.beginPath();
    ctx.arc(x, y, r + level * r * 0.715, 0, TAU); // 10 px at full level on the map
    ctx.fillStyle = `rgba(${ink},${0.04 + level * 0.12})`;
    ctx.fill();
  }

  // A badge with the instrument's icon; the selected one is filled. A player with several
  // tracks shows how many on a tab at the badge's shoulder.
  private badge(ctx: CanvasRenderingContext2D, player: Seat, x: number, y: number, r: number, chosen: boolean, ink: string, paper: string) {
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(x, y, r, 0, TAU);
    if (chosen) {
      ctx.fillStyle = `rgba(${ink},0.9)`;
      ctx.fill();
    } else {
      ctx.fillStyle = paper;
      ctx.fill();
      ctx.strokeStyle = `rgba(${ink},0.35)`;
      ctx.stroke();
    }
    drawIcon(ctx, playerIcon(player), x, y, r * 1.3, chosen ? paper : `rgb(${ink})`);

    if (player.tracks.length > 1) {
      const k = r / badgeRadius;
      const tx = x + r * 0.78;
      const ty = y - r * 0.78;
      ctx.save();
      ctx.beginPath();
      ctx.arc(tx, ty, 6.5 * k, 0, TAU);
      ctx.fillStyle = chosen ? paper : `rgb(${ink})`;
      ctx.fill();
      if (chosen) {
        ctx.strokeStyle = `rgba(${ink},0.9)`;
        ctx.stroke();
      }
      ctx.font = `600 ${8 * k}px "JetBrains Mono", monospace`;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillStyle = chosen ? `rgb(${ink})` : paper;
      ctx.fillText(String(player.tracks.length), tx, ty + 0.5 * k);
      ctx.restore();
    }
  }

  // The listener: an arrowhead, slim and notched so its direction reads at any angle (an
  // equilateral triangle turned 25° looks like it points left).
  private arrow(ctx: CanvasRenderingContext2D, x: number, y: number, degrees: number, alpha: number, ink: string, size = 1) {
    ctx.save();
    ctx.globalAlpha = alpha;
    ctx.translate(x, y);
    ctx.rotate((degrees * Math.PI) / 180);
    ctx.scale(size, size);
    ctx.beginPath();
    ctx.moveTo(0, -8);
    ctx.lineTo(5.5, 6);
    ctx.lineTo(0, 3);
    ctx.lineTo(-5.5, 6);
    ctx.closePath();
    ctx.fillStyle = `rgba(${ink},0.9)`;
    ctx.fill();
    ctx.restore();
  }

  private minimap(
    ctx: CanvasRenderingContext2D,
    W: number,
    stage: SeatedStage,
    drawn: ReadonlyMap<NodeKey, Point>,
    selected: NodeKey | null,
    viewer: Point, // where the eyes are, looking along `yaw`
    yaw: number,
    listener: Point | undefined, // when the eyes are a spectator's: the listener, a ring
    eyes: number,
    alpha: number,
    ink: string,
    paper: string,
  ) {
    const S = Math.round(Math.max(72, Math.min(112, W * 0.16)));
    const X = W - S - 14;
    const Y = 14;
    const k = S / stage.room;
    const mx = (x: number) => X + S / 2 + x * k;
    const my = (y: number) => Y + S / 2 - y * k;
    ctx.save();
    ctx.globalAlpha = alpha;
    ctx.lineWidth = 1;
    ctx.fillStyle = paper;
    ctx.fillRect(X, Y, S, S);
    ctx.strokeStyle = `rgba(${ink},0.25)`;
    ctx.strokeRect(X + 0.5, Y + 0.5, S - 1, S - 1);
    const turn = (yaw * Math.PI) / 180;
    const fov = ((view3d.fovDegrees / 2) * Math.PI) / 180;
    ctx.beginPath();
    ctx.moveTo(mx(viewer.x), my(viewer.y));
    ctx.arc(mx(viewer.x), my(viewer.y), S * 0.36, turn - Math.PI / 2 - fov, turn - Math.PI / 2 + fov);
    ctx.closePath();
    ctx.fillStyle = `rgba(${ink},0.1)`;
    ctx.fill();
    for (const player of stage.players) {
      const p = drawn.get(player.id)!;
      ctx.beginPath();
      ctx.arc(mx(p.x), my(p.y), player.id === selected ? 3 : 2, 0, TAU);
      ctx.fillStyle = `rgba(${ink},${player.id === selected ? 0.9 : 0.5})`;
      ctx.fill();
    }
    if (listener) {
      ctx.beginPath();
      ctx.arc(mx(listener.x), my(listener.y), 3.5, 0, TAU);
      ctx.strokeStyle = `rgba(${ink},0.9)`;
      ctx.stroke();
    }
    ctx.restore();
    this.arrow(ctx, mx(viewer.x), my(viewer.y), yaw, alpha, ink, 0.7);
    this.label(ctx, `EYES ${eyes.toFixed(2)} M`, X, Y + S + 12, 0.6 * alpha, ink, paper, "left");
  }

  // Text drawn straight onto the frame (in the room it moves every frame, so caching it is no use).
  private label(ctx: CanvasRenderingContext2D, text: string, x: number, y: number, opacity: number, ink: string, paper: string, align: CanvasTextAlign = "center", halo = true) {
    ctx.save();
    ctx.font = '500 10px "JetBrains Mono", monospace';
    ctx.letterSpacing = "0.14em";
    ctx.textAlign = align;
    ctx.textBaseline = "middle";
    if (halo) {
      ctx.lineJoin = "round";
      ctx.lineWidth = 3;
      ctx.strokeStyle = paper;
      ctx.globalAlpha = Math.min(1, opacity * 1.6);
      ctx.strokeText(text, x, y);
    }
    ctx.globalAlpha = 1;
    ctx.fillStyle = `rgba(${ink},${opacity})`;
    ctx.fillText(text, x, y);
    ctx.restore();
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
