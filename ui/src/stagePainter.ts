import { hasSeat, seatPitch, seatsIn, type Audience } from "./audience";
import type { Point } from "./bridge";
import { easeToward } from "./ease";
import { averageWood, buildRoom, buildSeats } from "./gl/roomMesh";
import { PathCollector, parseRgb, Tris, type RGBA } from "./gl/tris";
import { OverlayQuads } from "./gl/sprites";
import { WorldRenderer } from "./gl/world";
import type { Zoom } from "./mapEdits";
import { drawIcon, playerIcon, type IconKey } from "./icons";
import type { Levels } from "./level";
import { seatLevel, type SeatedStage } from "./players";
import { swoop, view3d, type Camera, type Pt } from "./room/camera";
import { buildFigures, partOrder, type FigureMesh } from "./gl/figureMesh";
import { distanceArcs, layoutRoom, type Layout, type Materials } from "./room/scene";

export type NodeKey = "listener" | number;
export type MapView = "grid" | "azimuth";
export type MapNames = "selected" | "all";

// A node on screen that can be clicked.
export type Hit = { key: NodeKey; x: number; y: number; r: number };
// Where the frame was drawn: the flat map's pixels per metre around (cx, cy), valid while
// `flat`; and the clickable nodes, nearest first.
export type View = { cx: number; cy: number; scale: number; flat: boolean; hits: Hit[] };

// The 3D view: on or off (the camera swoops between), which way the eyes look, seated or standing.
export type Look = { on: boolean; yaw: number; pitch: number; standing: boolean };

export type MapFrame = {
  width: number; // CSS pixels
  height: number;
  dpr: number; // device pixels per CSS pixel
  seconds: number; // since the map appeared; drives the breathing ring and the eases
  stage: SeatedStage;
  selected: NodeKey | null; // the one that leads (Edit player, the azimuth line)
  selection: readonly NodeKey[]; // every selected node, the leading one included
  zoom: Zoom; // the map's (the room's view starts from it)
  audience: Audience; // where the audience sits
  picked?: Audience; // seats picked while editing the audience (drawn stronger)
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

type Align = "left" | "center";
type Text = { text: string; x: number; y: number; opacity: number; halo: boolean; align: Align };

// What the 2D overlay draws, as data: it is drawn again only when this list changes (the
// levels and the breathing ring are in the WebGL frame, so a playing ensemble doesn't redraw it).
// exact: drawn as one sprite (on the map, where badges keep their size).
type Badge = { k: "badge"; icon: IconKey; tracks: number; x: number; y: number; r: number; chosen: boolean; exact: boolean };
type Label = { k: "label" } & Text;
type Arrow = { k: "arrow"; x: number; y: number; degrees: number; alpha: number; size: number };
type Minimap = {
  k: "minimap";
  x: number; // its left
  size: number;
  room: number;
  players: [number, number, boolean][]; // x, y (metres), selected
  viewer: [number, number];
  yaw: number;
  eyes: number;
  alpha: number;
};
type Op = Badge | Label | Arrow | Minimap;


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
//
// WebGL draws all of it (gl/world.ts): the room and the seats and the players are meshes the GPU
// keeps, the camera is in its shaders; the map's lines, halos and rings are triangles built for
// the frame; badges, names and labels are sprites drawn once with the 2D canvas (gl/sprites.ts).
// A frame where nothing changed is not drawn at all.
export class StagePainter {
  private drawn = new Map<NodeKey, Point>(); // eased positions in metres
  private shown = new Map<number, number>(); // eased input levels, 0..1
  private rose = -1; // 0 grid .. 1 azimuth, eased; -1 until the first frame
  private amount = new Glide(view3d.swoopMs / 1000); // 0 map .. 1 eyes
  private eyes = new Glide(0.7);
  private lastSeconds: number | undefined;

  private world: WorldRenderer;
  private before = new Tris();
  private after = new Tris();
  private players = new Tris();
  private path = new PathCollector();
  private figures: { key: string; mesh: FigureMesh } | undefined; // the players' mesh on the GPU
  private icons = new WeakMap<object, IconKey>(); // playerIcon's answers (it runs regular expressions)
  private overlayQuads = new OverlayQuads();
  private colours = new Map<string, [number, number, number]>(); // CSS colours as numbers
  private passKey = ""; // what `before` and `after` were built for
  private passTexts: Text[] = []; // and the labels built with them

  // What the GPU holds, and what it was built from.
  private layout: Layout | undefined;
  private roomFor: { room: number; materials: Materials } | undefined;
  private seatsBase: { stage: SeatedStage; audience: Audience; room: number; players: string; seats: Point[]; viewer?: Point } | undefined;
  private seatsKey = "";
  private lastWorld = "";
  private widths = new Map<string, number>(); // measured name widths, by text

  // Every node is where C++ says it is and no view is changing: the map may be drawn less often.
  settled = false;

  // The camera is down at the listener's eyes (walking starts there).
  get inRoom() {
    return this.amount.value > 0.999;
  }

  constructor(private canvas: HTMLCanvasElement) {
    this.world = new WorldRenderer(canvas);
    canvas.addEventListener("webglcontextlost", this.onLost);
    canvas.addEventListener("webglcontextrestored", this.onRestored);
  }

  dispose() {
    this.canvas.removeEventListener("webglcontextlost", this.onLost);
    this.canvas.removeEventListener("webglcontextrestored", this.onRestored);
    this.world.dispose();
  }

  // A lost context (the GPU was reset, the computer slept) takes everything on the GPU with it:
  // once it is back, all of it is built again.
  private onLost = (e: Event) => e.preventDefault(); // or it never comes back
  private onRestored = () => {
    this.world.dispose();
    this.world = new WorldRenderer(this.canvas);
    this.roomFor = undefined;
    this.figures = undefined;
    this.seatsBase = undefined;
    this.seatsKey = "";
    this.passKey = "";
    this.lastWorld = "";
  };

  paint(frame: MapFrame): View {
    const { width: W, height: H, stage, selected, overrides, look, materials: M, zoom } = frame;
    const ink = parseRgb(frame.ink);
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
    const viewer = listener; // where the eyes are in the room
    // In the room, straight ahead is the middle of the view below the tools (a second row of
    // them, in a narrow window, would cover the nearest names).
    const below = H + top - margin.top;
    const cam = swoop({ amount: e, width: W, height: below, map: { x: cx, y: cy, scale }, listener: viewer, eyes, yaw: look.yaw, pitch: look.pitch });
    const flat = cam.flat;
    const shows = smoothstep(0.15, 0.75, e); // how much of the room's materials show
    // Snap hairlines to the pixel grid so they stay crisp on the flat map.
    // (1e-6: the camera's float error must not tip a line that falls on a half pixel.)
    const px = (v: number) => Math.round(v + 1e-6) + 0.5;
    const snap = flat ? px : undefined;

    const { before, after, players, path } = this;
    players.clear();
    const ops: Op[] = [];
    const shotKey = Object.values(cam.shot).join(" ");

    // A path stroked once (where it crosses itself the colour is laid down once).
    const strokeOnce = (t: Tris, width: number, colour: RGBA, draw: (p: PathCollector) => void, dash?: readonly number[]) => {
      path.reset();
      draw(path);
      t.begin();
      path.stroke(t, width, colour, dash);
      t.end();
    };
    const line = (p: PathCollector, a: Pt, b: Pt) => cam.seg(p, a, b, snap);

    // The room's meshes: built again only when what they show changes.
    if (shows > 0.002) {
      if (this.layout?.room !== stage.room) this.layout = layoutRoom(stage.room);
      if (this.roomFor?.room !== stage.room || this.roomFor.materials !== M) {
        this.world.setRoom(buildRoom(this.layout, M));
        this.roomFor = { room: stage.room, materials: M };
        this.seatsKey = ""; // their colours too
      }
    }

    // The map's lines, the distance arcs, the shadows and the audience on the map change only
    // with the camera, the nodes and the look: built again only then (not for the levels).
    const audience = seatsIn(frame.audience, stage.room);
    const passKey = [
      shotKey, W, H, this.rose, e, zoom.k, zoom.x, zoom.y, frame.ink, materialsId(M), frame.painting, selected, look.yaw,
      [...drawn].map(([k, p]) => `${k}:${p.x},${p.y}`).join(";"),
      bytesKey(frame.audience),
      frame.picked ? bytesKey(frame.picked) : 0,
    ].join("|");
    if (passKey !== this.passKey) {
      this.passKey = passKey;
      before.clear();
      after.clear();
      const texts: Text[] = (this.passTexts = []);

      // Grid: 1 m minor (skipped when too dense, gone in the azimuth view), 5 m major, counted
      // from the centre, and the room's outline. In the room it would read as tiles, halving
      // every distance, so it fades out there.
      const grid = 1 - shows;
      if (grid > 0.002) {
        for (const major of [false, true]) {
          if (!major && (scale < 6 || this.rose > 0.999)) continue;
          strokeOnce(before, 1, [ink[0], ink[1], ink[2], (major ? 0.1 : 0.05 * (1 - this.rose)) * grid], (p) => {
            for (let m = -Math.floor(half); m <= half; m++) {
              if (Math.abs(m) >= half || (m % 5 === 0) !== major) continue; // the outline covers the edges
              line(p, [m, half, 0], [m, -half, 0]);
              line(p, [-half, m, 0], [half, m, 0]);
            }
          });
        }
        strokeOnce(before, 1, [ink[0], ink[1], ink[2], 0.2 * grid], (p) => {
          line(p, [-half, half, 0], [half, half, 0]);
          line(p, [half, half, 0], [half, -half, 0]);
          line(p, [half, -half, 0], [-half, -half, 0]);
          line(p, [-half, -half, 0], [-half, half, 0]);
        });
      }

      // Azimuth view: a circle inscribed in the room, ticks every 5 and 15 degrees (inwards),
      // labels every 30 just outside. Angles as the DSP counts them: 0 ahead, +90 right.
      const roseShows = this.rose * (1 - 0.6 * shows);
      if (roseShows > 0.001) {
        const R = half;
        const at = (deg: number, r: number): Pt => [Math.sin((deg * Math.PI) / 180) * r, Math.cos((deg * Math.PI) / 180) * r, 0];
        strokeOnce(before, 1, [ink[0], ink[1], ink[2], 0.2 * roseShows], (p) => {
          for (let deg = 0; deg < 360; deg += 2.5) cam.seg(p, at(deg, R), at(deg + 2.5, R));
        });
        strokeOnce(before, 1, [ink[0], ink[1], ink[2], 0.07 * roseShows], (p) => {
          for (let deg = 30; deg < 180; deg += 30) cam.seg(p, at(deg, R), at(deg + 180, R));
        });
        strokeOnce(before, 1, [ink[0], ink[1], ink[2], 0.14 * roseShows], (p) => line(p, at(0, R), at(180, R)));
        for (const [step, len, alpha] of [[5, 0.28, 0.25], [15, 0.55, 0.45]] as const)
          strokeOnce(before, 1, [ink[0], ink[1], ink[2], alpha * roseShows], (p) => {
            for (let deg = 0; deg < 360; deg += step) {
              if (step === 5 && deg % 15 === 0) continue;
              cam.seg(p, at(deg, R), at(deg, R - len));
            }
          });
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
          strokeOnce(before, 1, [ink[0], ink[1], ink[2], 0.45 * (1 - e)], (p) => {
            p.moveTo(a[0], y);
            p.lineTo(b[0], y);
            p.moveTo(px(a[0]), y - 3);
            p.lineTo(px(a[0]), y + 3);
            p.moveTo(px(b[0]), y - 3);
            p.lineTo(px(b[0]), y + 3);
          });
          texts.push({ text: `${metres} M`, x: b[0] + 10, y, opacity: 0.55 * (1 - e), halo: false, align: "left" });
        }
      }

      // In the room: where 3, 5 and 10 m really are (dashed arcs on the floor), and a soft vignette.
      if (shows > 0.002) {
        const yaw = (look.yaw * Math.PI) / 180;
        const on = (a: number, r: number): Pt => [viewer.x + Math.sin(a) * r, viewer.y + Math.cos(a) * r, 0.01];
        for (const r of distanceArcs)
          strokeOnce(before, 1, [ink[0], ink[1], ink[2], 0.35 * shows], (p) => {
            for (let a = -60; a < 60; a += 3) cam.seg(p, on(yaw + (a * Math.PI) / 180, r), on(yaw + ((a + 3) * Math.PI) / 180, r));
          }, [4, 6]);
        for (const r of distanceArcs) {
          const p = cam.screen(on(yaw, r), 0.3);
          if (p) texts.push({ text: `${r} M`, x: p[0], y: p[1] - 8, opacity: 0.7 * shows, halo: true, align: "center" });
        }
      }

      // Azimuth view: a dashed line from the listener to the selected player.
      const sel = selected !== null ? drawn.get(selected) : undefined;
      const dash = this.rose * (1 - e);
      if (dash > 0.001 && sel && selected !== "listener")
        strokeOnce(after, 1, [ink[0], ink[1], ink[2], 0.35 * dash], (p) => cam.seg(p, [listener.x, listener.y, 0], [sel.x, sel.y, 0]), [2, 3]);

      // The audience on the map: a square for each seat (stronger while it is being edited,
      // strongest when picked).
      if (e < 0.5) {
        const fade = 1 - 2 * e;
        const colour: RGBA = [ink[0], ink[1], ink[2], (frame.painting ? 0.16 : 0.07) * fade];
        const strong: RGBA = [ink[0], ink[1], ink[2], 0.5 * fade];
        const s = seatPitch * 0.35;
        const picked = frame.picked;
        for (const p of audience) {
          const on = picked && hasSeat(picked, Math.round(p.x / seatPitch), Math.round(p.y / seatPitch));
          if (cam.poly(path.reset(), [[p.x - s, p.y - s, 0], [p.x + s, p.y - s, 0], [p.x + s, p.y + s, 0], [p.x - s, p.y + s, 0]])) path.fill(after, on ? strong : colour);
        }
      }
    }
    const texts: Text[] = [...this.passTexts];

    // Audience seats (in the room only), clear of the players and of the eyes.
    if (shows > 0.002) this.updateSeats(frame, audience, viewer, M);

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
        this.ring(players, x, y, key === "listener" ? 11 : badgeRadius, frame.seconds, ink);
      }
      for (const player of stage.players) {
        const [x, y] = at(drawn.get(player.id)!);
        this.halo(players, x, y, badgeRadius, shown.get(player.id) ?? 0, ink);
      }
      for (const player of byPriority) {
        if (frame.names !== "all" && !chosen(player.id)) continue;
        const [x, y] = at(drawn.get(player.id)!);
        texts.push({ text: player.name.toUpperCase(), x, y: y + badgeRadius + 10, opacity: chosen(player.id) ? 0.85 : 0.55, halo: true, align: "center" });
      }
      for (const t of texts) ops.push({ k: "label", ...t });
      const [lx, ly] = at(listener);
      ops.push({ k: "arrow", x: lx, y: ly, degrees: frame.facing, alpha: 1, size: 1 });
      hits.push({ key: "listener", x: lx, y: ly, r: hitRadius });
      for (const player of byPriority) {
        const [x, y] = at(drawn.get(player.id)!);
        ops.push({ k: "badge", icon: this.iconOf(player), tracks: player.tracks.length, x, y, r: badgeRadius, chosen: chosen(player.id), exact: true });
        hits.push({ key: player.id, x, y, r: hitRadius });
      }
      this.draw(frame, cam, shows, ops, undefined);
      return { cx, cy, scale, flat, hits };
    }

    // The swoop and the room: everything by depth, far to near, so nearer covers farther.
    // Badges rise to pins just over the players' heads (the top of a seated head is at 1.37 m)
    // and grow with the room's scale (to 48 px); a name sits over its badge.
    for (const t of texts) ops.push({ k: "label", ...t });
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
    const placed: { x0: number; x1: number; y0: number; y1: number }[] = [];
    for (const o of [...items].sort((a, b) => Number(b.chosen) - Number(a.chosen) || a.c.z - b.c.z || b.order - a.order)) {
      const n = o.name;
      if (!n) continue;
      const w = this.textWidth(n.text) / 2 + 4;
      const box = { x0: n.x - w, x1: n.x + w, y0: n.y - 8, y1: n.y + 8 };
      if (placed.some((b) => box.x0 < b.x1 && b.x0 < box.x1 && box.y0 < b.y1 && b.y0 < box.y1)) o.name = undefined;
      else placed.push(box);
    }

    // The figures and their shadows are a mesh on the GPU, built again only when a player moves
    // or the selection changes; a frame only puts their parts in order, far to near.
    let order: Uint16Array | Uint32Array | undefined;
    if (shows > 0.002) {
      const mesh = this.figureMesh(stage, chosen);
      let n = 0;
      const runs: [number, number][] = [];
      for (const { player, p } of items) {
        const parts = mesh.players.get(player.id);
        if (!parts || cam.toCam(p.x, p.y, 0).z < 0.3) continue; // at the eyes' feet: not drawn
        const toCentre = Math.hypot(p.x, p.y);
        const fx = toCentre > 0.05 ? -p.x / toCentre : 0;
        const fy = toCentre > 0.05 ? -p.y / toCentre : -1;
        const front = fx * (cam.x - p.x) + fy * (cam.y - p.y) >= 0; // we see their face
        for (const run of partOrder(parts, front)) {
          runs.push(run);
          n += run[1];
        }
      }
      order = this.world.indexArray(n);
      let i = 0;
      for (const [first, count] of runs) for (let k = 0; k < count; k++) order[i++] = first + k;
    }

    for (const { player, c, chosen, x, y, rb, name } of items) {
      players.depth = c.z;
      this.halo(players, x, y, rb, shown.get(player.id) ?? 0, ink);
      if (chosen) this.ring(players, x, y, rb, frame.seconds, ink);
      ops.push({ k: "badge", icon: this.iconOf(player), tracks: player.tracks.length, x, y, r: rb, chosen, exact: false });
      if (name) ops.push({ k: "label", ...name });
      hits.unshift({ key: player.id, x, y, r: rb + 2 }); // nearest first
    }
    players.depth = 0;

    // The listener's arrow fades out as the camera comes down. In the room it would lie on the
    // floor (user decision, 2026-11-26); the minimap shows where it stands.
    if (e < 0.999) {
      const p = cam.screen([listener.x, listener.y, 0], 0.1);
      if (p) {
        ops.push({ k: "arrow", x: p[0], y: p[1], degrees: frame.facing - cam.shot.yaw, alpha: 1 - e, size: 1 });
        if (e < 0.5) hits.push({ key: "listener", x: p[0], y: p[1], r: hitRadius });
      }
    }

    // Walking: a minimap with where you are and where you look.
    const mini = smoothstep(0.6, 1, e);
    if (mini > 0.002) {
      const S = Math.round(Math.max(72, Math.min(112, W * 0.16)));
      ops.push({
        k: "minimap",
        x: W - S - 14,
        size: S,
        room: stage.room,
        players: stage.players.map((pl) => {
          const q = drawn.get(pl.id)!;
          return [q.x, q.y, pl.id === selected];
        }),
        viewer: [viewer.x, viewer.y],
        yaw: look.yaw,
        eyes,
        alpha: mini,
      });
    }

    this.draw(frame, cam, shows, ops, order);
    return { cx, cy, scale, flat, hits };
  }

  // The figures' mesh for these players where they are drawn now, with this selection.
  private figureMesh(stage: SeatedStage, chosen: (key: NodeKey) => boolean): FigureMesh {
    const list = stage.players.map((p) => {
      const at = this.drawn.get(p.id)!;
      return { id: p.id, x: at.x, y: at.y, selected: chosen(p.id) };
    });
    const key = list.map((p) => `${p.id}:${p.x},${p.y},${p.selected ? 1 : 0}`).join(";");
    if (this.figures?.key !== key) {
      const mesh = buildFigures(list);
      this.world.setFigures(mesh.data);
      this.figures = { key, mesh };
    }
    return this.figures.mesh;
  }

  // A player object lives until the next stage from C++, so its icon is worked out once per stage.
  private iconOf(player: SeatedStage["players"][number]): IconKey {
    let icon = this.icons.get(player);
    if (icon === undefined) this.icons.set(player, (icon = playerIcon(player)));
    return icon;
  }

  // The seats' mesh: which seats show depends on the audience, the room, where the players sit
  // and where the eyes are; rebuilt only when that changes.
  private updateSeats(frame: MapFrame, audience: Point[], viewer: Point, M: Materials) {
    const { stage } = frame;
    let base = this.seatsBase;
    if (!base || base.stage !== stage || base.audience !== frame.audience) {
      const players = stage.players.map((p) => `${p.x},${p.y}`).join(";");
      if (!base || base.audience !== frame.audience || base.room !== stage.room || base.players !== players) {
        const seats = audience.filter((s) => stage.players.every((p) => Math.hypot(s.x - p.x, s.y - p.y) > 0.75));
        base = this.seatsBase = { stage, audience: frame.audience, room: stage.room, players, seats };
        this.seatsKey = "";
      } else base.stage = stage;
    }
    // Which seats are too close to the eyes changes only when they move.
    if (this.seatsKey && base.viewer && Math.abs(base.viewer.x - viewer.x) < 0.005 && Math.abs(base.viewer.y - viewer.y) < 0.005) return;
    base.viewer = { ...viewer };
    const hidden: number[] = [];
    base.seats.forEach((s, i) => {
      if (Math.hypot(s.x - viewer.x, s.y - viewer.y) <= 0.5) hidden.push(i);
    });
    const key = `seats:${hidden.join()}`;
    if (key === this.seatsKey) return;
    this.seatsKey = key;
    this.world.setSeats(buildSeats(base.seats.filter((_, i) => !hidden.includes(i)), M));
  }

  // Draws the frame when anything in it changed (the levels change it while the music plays).
  private draw(frame: MapFrame, cam: Camera, shows: number, ops: Op[], figures: Uint16Array | Uint32Array | undefined) {
    const { width: W, height: H, dpr, materials: M } = frame;
    const ceilingShows = shows * Math.min(1, Math.max(0, (view3d.ceiling - cam.z) / 1.5));
    const overlayKey = JSON.stringify([fontLoads, frame.ink, frame.paper, ops]);
    const stillKey = [
      W, H, dpr, shows, ceilingShows, materialsId(M), frame.ink, this.seatsKey, this.roomFor?.room,
      Object.values(cam.shot).join(" "),
      this.passKey, this.figures?.key, figures ? figures.length : 0,
    ].join("|");
    const worldText = `${stillKey}|${hashOf(this.players)}|${overlayKey}`;
    if (worldText === this.lastWorld) return;
    this.lastWorld = worldText;
    const atlas = this.world.atlas;
    const built = atlas.generation;
    let overlay = this.overlayOf(frame, ops);
    if (atlas.generation !== built) overlay = this.overlayOf(frame, ops); // the atlas was full and started again halfway
    this.world.render({
      shot: cam.shot,
      width: W,
      height: H,
      dpr,
      shows,
      ceilingShows,
      fog: M.fog,
      seam: M.seam,
      average: averageWood(M),
      vignette: { shade: parseRgb(M.shade), alpha: M.vignette * shows },
      before: this.before,
      after: this.after,
      ink: parseRgb(frame.ink),
      shadows: this.figures && { first: this.figures.mesh.shadows[0], count: this.figures.mesh.shadows[1], shade: parseRgb(M.shade), alpha: M.shadow * shows },
      figures,
      stillKey,
      players: this.players,
      overlay,
    });
  }

  // The badges, names, labels, the listener's arrow and the minimap, in the order the canvas
  // drew them.
  private overlayOf(frame: MapFrame, ops: Op[]): OverlayQuads {
    const { dpr, ink, paper } = frame;
    const quads = this.overlayQuads;
    quads.clear();
    const inkRgb = parseRgb(ink);
    const paperRgb = this.colourOf(paper);
    for (const op of ops) {
      if (op.k === "label") this.labelQuads(quads, op, ink, paper, dpr);
      else if (op.k === "badge") this.badgeQuads(quads, op, ink, paper, inkRgb, paperRgb, dpr);
      else if (op.k === "arrow") this.arrow(quads, op.x, op.y, op.degrees, op.alpha, inkRgb, op.size);
      else {
        this.minimap(quads, op, inkRgb, paperRgb);
        const eyes: Text = { text: `EYES ${op.eyes.toFixed(2)} M`, x: op.x, y: 14 + op.size + 12, opacity: 0.6 * op.alpha, halo: true, align: "left" };
        this.labelQuads(quads, eyes, ink, paper, dpr);
      }
    }
    return quads;
  }

  // A label as two sprites: its halo, then its letters, each at the opacity the canvas drew
  // them with (the halo 1.6 times stronger, as `label` does).
  private labelQuads(quads: OverlayQuads, t: Text, ink: string, paper: string, dpr: number) {
    const w = this.textWidth(t.text) + 8;
    const ax = t.align === "center" ? w / 2 : 4;
    const base = `${t.text}|${t.align}|${dpr}|${fontLoads}`;
    const part = (halo: boolean) =>
      this.world.atlas.get(`label|${halo ? `halo|${paper}` : `text|${ink}`}|${base}`, w, 20, ax, 10, dpr, (c) =>
        this.label(c, { ...t, x: 0, y: 0, opacity: 1, halo }, ink, paper, halo ? "halo" : "text"),
      );
    if (t.halo) quads.sprite(part(true), t.x, t.y, t.opacity * 1.6, dpr);
    quads.sprite(part(false), t.x, t.y, t.opacity, dpr);
  }

  // On the map a badge is one sprite, exactly as the canvas drew it. In the room it changes
  // size with every step, so it is put together: disc and outline, icon, the tab with its count.
  private badgeQuads(quads: OverlayQuads, b: Badge, ink: string, paper: string, inkRgb: readonly number[], paperRgb: readonly number[], dpr: number) {
    const { x, y, r, chosen } = b;
    const k = r / badgeRadius;
    if (b.exact) {
      const reach = Math.ceil(Math.max(r + 1, b.tracks > 1 ? r * 0.78 + 6.5 * k + 1 : 0)) + 1;
      const key = `badge|${b.icon}|${r}|${chosen}|${b.tracks}|${ink}|${paper}|${dpr}`;
      quads.sprite(this.world.atlas.get(key, reach * 2, reach * 2, reach, reach, dpr, (c) => this.badge(c, { ...b, x: 0, y: 0 }, ink, paper)), x, y, 1, dpr);
      return;
    }
    const i = (a: number): RGBA => [inkRgb[0], inkRgb[1], inkRgb[2], a];
    const p = (a: number): RGBA => [paperRgb[0], paperRgb[1], paperRgb[2], a];
    if (chosen) quads.disc(x, y, r, i(0.9));
    else {
      quads.disc(x, y, r, p(1));
      quads.ring(x, y, r, 1, i(0.35));
    }
    quads.icon(b.icon, x, y, r * 1.3, chosen ? p(1) : i(1));
    if (b.tracks > 1) {
      const tx = x + r * 0.78;
      const ty = y - r * 0.78;
      quads.disc(tx, ty, 6.5 * k, chosen ? p(1) : i(1));
      if (chosen) quads.ring(tx, ty, 6.5 * k, 1, i(0.9));
      const kq = Math.round(k * 8) / 8; // the count's size, in steps too small to see
      const colour = chosen ? `rgb(${ink})` : paper;
      const count = this.world.atlas.get(`count|${b.tracks}|${kq}|${colour}|${dpr}|${fontLoads}`, 24 * kq + 4, 12 * kq + 4, 12 * kq + 2, 6 * kq + 2, dpr, (c) => {
        c.font = `600 ${8 * kq}px "JetBrains Mono", monospace`;
        c.textAlign = "center";
        c.textBaseline = "middle";
        c.fillStyle = colour;
        c.fillText(String(b.tracks), 0, 0);
      });
      quads.sprite(count, tx, ty + 0.5 * k, 1, dpr);
    }
  }

  // A CSS colour (the --paper token) as numbers.
  private colourOf(css: string): [number, number, number] {
    let rgb = this.colours.get(css);
    if (!rgb) {
      const ctx = this.world.atlas.measure;
      ctx.fillStyle = "#000";
      ctx.fillStyle = css;
      const v = String(ctx.fillStyle);
      rgb = v.startsWith("#") ? [parseInt(v.slice(1, 3), 16), parseInt(v.slice(3, 5), 16), parseInt(v.slice(5, 7), 16)] : parseRgb(v.replace(/^rgba?\(|\)$/g, ""));
      this.colours.set(css, rgb);
    }
    return rgb;
  }

  private textWidth(text: string) {
    let w = this.widths.get(text);
    if (w === undefined) {
      const ctx = this.world.atlas.measure;
      ctx.save();
      ctx.font = '500 10px "JetBrains Mono", monospace';
      ctx.letterSpacing = "0.14em";
      w = ctx.measureText(text).width;
      ctx.restore();
      if (this.widths.size > 512) this.widths.clear();
      this.widths.set(text, w);
    }
    return w;
  }

  private ring(t: Tris, x: number, y: number, r: number, seconds: number, ink: readonly number[]) {
    const b = breath(seconds, 2.6);
    t.ring(x, y, r * (1.3 + 0.35 * b), 1, [ink[0], ink[1], ink[2], 0.12 + 0.3 * (1 - b)]);
  }

  // Input level: a soft halo around a badge (30 Hz from C++, eased per frame).
  private halo(t: Tris, x: number, y: number, r: number, level: number, ink: readonly number[]) {
    if (level < 0.01) return;
    t.disc(x, y, r + level * r * 0.715, [ink[0], ink[1], ink[2], 0.04 + level * 0.12]); // 10 px at full level on the map
  }

  // A badge with the instrument's icon; the selected one is filled. A player with several
  // tracks shows how many on a tab at the badge's shoulder.
  private badge(ctx: CanvasRenderingContext2D, b: Badge, ink: string, paper: string) {
    const { x, y, r, chosen } = b;
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
    drawIcon(ctx, b.icon, x, y, r * 1.3, chosen ? paper : `rgb(${ink})`);

    if (b.tracks > 1) {
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
      ctx.fillText(String(b.tracks), tx, ty + 0.5 * k);
      ctx.restore();
    }
  }

  // The listener: an arrowhead, slim and notched so its direction reads at any angle (an
  // equilateral triangle turned 25° looks like it points left).
  private arrow(t: OverlayQuads, x: number, y: number, degrees: number, alpha: number, ink: readonly number[], size = 1) {
    const a = (degrees * Math.PI) / 180;
    const cos = Math.cos(a) * size;
    const sin = Math.sin(a) * size;
    const at = (px: number, py: number): [number, number] => [x + px * cos - py * sin, y + px * sin + py * cos];
    const pts = [at(0, -8), at(5.5, 6), at(0, 3), at(-5.5, 6)];
    // Fanned from the tip, which sees every corner (the notch makes it concave).
    t.polygon(pts.map((p) => p[0]), pts.map((p) => p[1]), [ink[0], ink[1], ink[2], 0.9 * alpha]);
  }

  private minimap(t: OverlayQuads, m: Minimap, ink: readonly number[], paper: readonly number[]) {
    const { size: S, x: X, alpha } = m;
    const Y = 14;
    const k = S / m.room;
    const mx = (x: number) => X + S / 2 + x * k;
    const my = (y: number) => Y + S / 2 - y * k;
    const c = (a: number): RGBA => [ink[0], ink[1], ink[2], a * alpha];
    t.rect(X, Y, X + S, Y + S, [paper[0], paper[1], paper[2], alpha]);
    // Its 1 px outline, as four strips that don't overlap (each pixel laid down once).
    t.rect(X, Y, X + S, Y + 1, c(0.25));
    t.rect(X, Y + S - 1, X + S, Y + S, c(0.25));
    t.rect(X, Y + 1, X + 1, Y + S - 1, c(0.25));
    t.rect(X + S - 1, Y + 1, X + S, Y + S - 1, c(0.25));
    const turn = (m.yaw * Math.PI) / 180;
    const fov = ((view3d.fovDegrees / 2) * Math.PI) / 180;
    const [vx, vy] = m.viewer;
    const xs = [mx(vx)];
    const ys = [my(vy)];
    for (let i = 0; i <= 32; i++) {
      const a = turn - Math.PI / 2 - fov + (2 * fov * i) / 32;
      xs.push(mx(vx) + Math.cos(a) * S * 0.36);
      ys.push(my(vy) + Math.sin(a) * S * 0.36);
    }
    t.polygon(xs, ys, c(0.1));
    for (const [x, y, selected] of m.players) t.disc(mx(x), my(y), selected ? 3 : 2, c(selected ? 0.9 : 0.5));
    this.arrow(t, mx(vx), my(vy), m.yaw, alpha, ink, 0.7);
  }

  // `only`: just the halo or just the letters (labelQuads keeps them apart).
  private label(ctx: CanvasRenderingContext2D, t: Text, ink: string, paper: string, only?: "halo" | "text") {
    ctx.save();
    ctx.font = '500 10px "JetBrains Mono", monospace';
    ctx.letterSpacing = "0.14em";
    ctx.textAlign = t.align;
    ctx.textBaseline = "middle";
    if (t.halo && only !== "text") {
      ctx.lineJoin = "round";
      ctx.lineWidth = 3;
      ctx.strokeStyle = paper;
      ctx.globalAlpha = Math.min(1, t.opacity * 1.6);
      ctx.strokeText(t.text, t.x, t.y);
    }
    if (only !== "halo") {
      ctx.globalAlpha = 1;
      ctx.fillStyle = `rgba(${ink},${t.opacity})`;
      ctx.fillText(t.text, t.x, t.y);
    }
    ctx.restore();
  }
}

// A cheap fingerprint of a frame's triangles: equal frames have equal ones, and a changed level
// or position changes it (FNV-1a over the float bits).
const bits = new Uint32Array(1);
const asFloat = new Float32Array(bits.buffer);
function hashOf(t: Tris) {
  let h = 2166136261 ^ t.length;
  const d = t.data;
  for (let i = 0; i < t.length; i++) {
    asFloat[0] = d[i];
    h = Math.imul(h ^ bits[0], 16777619);
  }
  return `${t.length}:${h >>> 0}:${t.groups.length}`;
}

// The audience's bytes as a key (painting changes them in place).
function bytesKey(bytes: Uint8Array) {
  let h = 2166136261;
  for (let i = 0; i < bytes.length; i++) h = Math.imul(h ^ bytes[i], 16777619);
  return h >>> 0;
}

// Materials are read again when the theme changes (a new object): numbered, for the frame's key.
const materialIds = new WeakMap<Materials, number>();
let nextMaterialId = 1;
function materialsId(M: Materials) {
  let id = materialIds.get(M);
  if (id === undefined) materialIds.set(M, (id = nextMaterialId++));
  return `m${id}`;
}
