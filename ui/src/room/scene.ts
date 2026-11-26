// The room around the stage in the 3D view: floorboards, panelled walls with doors and windows,
// a beamed ceiling, audience seats. Sizes everyone knows (a 2.1 m door, a 1.2 m window over a
// radiator, a seat) carry the scale. The layout depends only on the room size (the seats are
// where the user painted them: audience.ts); materials come from styles/room.css.
import { view3d, type Camera, type Pt } from "./camera";

export type RGB = readonly [number, number, number];

export type Materials = {
  wood: RGB[];
  seam: RGB;
  wall: RGB[];
  wainscot: RGB;
  skirting: RGB;
  ceiling: RGB;
  beam: RGB;
  door: RGB;
  glass: RGB;
  seat: RGB;
  seatBack: RGB;
  fog: RGB;
  shade: string; // "r,g,b"
  shadow: number; // alpha
  vignette: number;
};

export function readMaterials(style: CSSStyleDeclaration): Materials {
  const rgb = (name: string): RGB => {
    const [r, g, b] = style.getPropertyValue(`--room-${name}`).split(",").map(Number);
    return [r || 0, g || 0, b || 0];
  };
  return {
    wood: [1, 2, 3, 4, 5].map((i) => rgb(`wood-${i}`)),
    seam: rgb("seam"),
    wall: [rgb("wall-1"), rgb("wall-2")],
    wainscot: rgb("wainscot"),
    skirting: rgb("skirting"),
    ceiling: rgb("ceiling"),
    beam: rgb("beam"),
    door: rgb("door"),
    glass: rgb("glass"),
    seat: rgb("seat"),
    seatBack: rgb("seat-back"),
    fog: rgb("fog"),
    shade: style.getPropertyValue("--room-shade-rgb").trim() || "0,0,0",
    shadow: Number(style.getPropertyValue("--room-shadow")) || 0.2,
    vignette: Number(style.getPropertyValue("--room-vignette")) || 0.16,
  };
}

// ---- Layout ----

type Plank = { x0: number; x1: number; y0: number; y1: number; tone: number };
// A wall, by which side of the room it closes: u runs along it, left to right as seen from inside.
export type Wall = "front" | "back" | "right" | "left";
type Opening = { wall: Wall; at: number }; // `at`: where along the wall it starts, metres
export type Layout = { room: number; planks: Plank[]; doors: Opening[]; windows: Opening[] };

const doorWidth = 0.9;
const windowWidth = 1.2;

export function layoutRoom(room: number): Layout {
  const half = room / 2;

  // Boards 18 cm wide in random lengths, the same pattern every time.
  const planks: Plank[] = [];
  let seed = 11;
  const random = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
  for (let x = -half; x < half - 1e-6; x += 0.18) {
    let y = -half - random() * 2;
    while (y < half) {
      const length = 1.2 + random() * 1.6;
      const y0 = Math.max(-half, y);
      const y1 = Math.min(half, y + length);
      if (y1 > y0) planks.push({ x0: x, x1: Math.min(half, x + 0.18), y0, y1, tone: Math.floor(random() * 5) });
      y += length;
    }
  }

  // Doors at the back of the side walls and on the front wall; windows along the side walls
  // wherever there is no door.
  const doors = ([
    { wall: "left", at: -0.6 * half },
    { wall: "right", at: 0.2 * half },
    { wall: "front", at: 0.6 * half },
  ] satisfies Opening[]).filter((d) => d.at + doorWidth <= half - 0.3);
  const windows: Opening[] = [];
  for (const wall of ["left", "right"] as const)
    for (let at = -half + 1.4; at + windowWidth <= half - 1; at += 4) {
      const clear = doors.every((d) => d.wall !== wall || at + windowWidth + 0.4 < d.at || at > d.at + doorWidth + 0.4);
      if (clear) windows.push({ wall, at });
    }

  return { room, planks, doors, windows };
}

// A point on a wall's inner face: `u` along it, `z` up, `inset` into the room.
export function onWall(wall: Wall, half: number, u: number, z: number, inset = 0): Pt {
  switch (wall) {
    case "front": return [u, half - inset, z];
    case "back": return [-u, -half + inset, z];
    case "right": return [half - inset, -u, z];
    case "left": return [-half + inset, u, z];
  }
}

// ---- Drawing ----

const mix = (a: RGB, b: RGB, k: number) => `rgb(${(a[0] + (b[0] - a[0]) * k) | 0},${(a[1] + (b[1] - a[1]) * k) | 0},${(a[2] + (b[2] - a[2]) * k) | 0})`;

// How far into the fog something is, 0..0.72: distance in metres, times how much of the room shows.
export type Fog = (distance: number) => number;

// Ceiling, walls, doors, windows and floor. `shows`: how much of the room shows (0 on the map).
export function drawRoom(ctx: CanvasRenderingContext2D, cam: Camera, layout: Layout, M: Materials, shows: number, fog: Fog) {
  const half = layout.room / 2;
  const ceiling = view3d.ceiling;
  const far = (x: number, y: number, z: number) => fog(Math.hypot(x - cam.x, y - cam.y, z - cam.z));
  const fill = (pts: Pt[], colour: string, edge = colour) => {
    ctx.beginPath();
    if (!cam.poly(ctx, pts)) return;
    ctx.fillStyle = colour;
    ctx.fill();
    ctx.strokeStyle = edge;
    ctx.stroke(); // closes the hairline gaps between neighbouring faces
  };
  ctx.lineWidth = 0.8;

  // Ceiling with beams: only once the camera is under it.
  const ceilingShows = shows * Math.min(1, Math.max(0, (ceiling - cam.z) / 1.5));
  if (ceilingShows > 0.002) {
    ctx.globalAlpha = ceilingShows;
    const tile = Math.max(2.5, layout.room / 16);
    for (let x = -half; x < half - 1e-6; x += tile)
      for (let y = -half; y < half - 1e-6; y += tile) {
        const x1 = Math.min(half, x + tile);
        const y1 = Math.min(half, y + tile);
        const k = far((x + x1) / 2, (y + y1) / 2, ceiling);
        fill([[x, y, ceiling], [x1, y, ceiling], [x1, y1, ceiling], [x, y1, ceiling]], mix(M.ceiling, M.fog, k));
        const beam = mix(M.beam, M.fog, k);
        fill([[x, y, ceiling - 0.02], [x + 0.3, y, ceiling - 0.02], [x + 0.3, y1, ceiling - 0.02], [x, y1, ceiling - 0.02]], beam, beam);
      }
  }

  // Walls: panels 2 m wide, a wainscot up to 1.05 m, a skirting board.
  ctx.globalAlpha = shows;
  const panel = 2;
  for (const wall of ["front", "back", "right", "left"] as const)
    for (let i = 0, u0 = -half; u0 < half - 1e-6; i++, u0 += panel) {
      const u1 = Math.min(half, u0 + panel);
      const P = (u: number, z: number) => onWall(wall, half, u, z);
      const mid = P((u0 + u1) / 2, 2);
      const k = far(mid[0], mid[1], mid[2]);
      const upper = mix(M.wall[i % 2], M.fog, k);
      const lower = mix(M.wainscot, M.fog, k);
      const base = mix(M.skirting, M.fog, k);
      ctx.lineWidth = 0.8;
      fill([P(u0, 1.05), P(u1, 1.05), P(u1, ceiling), P(u0, ceiling)], upper);
      fill([P(u0, 0.14), P(u1, 0.14), P(u1, 1.05), P(u0, 1.05)], lower);
      fill([P(u0, 0), P(u1, 0), P(u1, 0.14), P(u0, 0.14)], base);
      ctx.beginPath();
      cam.seg(ctx, P(u0, 1.05), P(u1, 1.05));
      cam.seg(ctx, P(u0, 0.14), P(u0, 1.05));
      ctx.lineWidth = 1.2;
      ctx.strokeStyle = base;
      ctx.stroke();
    }

  // Doors, 0.9 x 2.1 m, in a frame.
  ctx.lineWidth = 0.8;
  for (const d of layout.doors) {
    const P = (u: number, z: number) => onWall(d.wall, half, d.at + u, z, 0.02);
    const mid = P(0.45, 1.05);
    const k = far(mid[0], mid[1], mid[2]);
    const frame = mix(M.skirting, M.fog, k);
    fill([P(-0.08, 0), P(doorWidth + 0.08, 0), P(doorWidth + 0.08, 2.18), P(-0.08, 2.18)], frame);
    fill([P(0, 0), P(doorWidth, 0), P(doorWidth, 2.1), P(0, 2.1)], mix(M.door, M.fog, k));
  }

  // Windows 1.2 x 1.6 m, sill at 0.93 m, a radiator under each.
  for (const w of layout.windows) {
    const P = (u: number, z: number) => onWall(w.wall, half, w.at + u, z, 0.02);
    const mid = P(0.6, 1.8);
    const k = far(mid[0], mid[1], mid[2]);
    const frame = mix(M.skirting, M.fog, k);
    ctx.lineWidth = 0.8;
    fill([P(-0.07, 0.93), P(1.27, 0.93), P(1.27, 2.67), P(-0.07, 2.67)], frame);
    fill([P(0, 1), P(1.2, 1), P(1.2, 2.6), P(0, 2.6)], mix(M.glass, M.fog, k));
    ctx.beginPath();
    cam.seg(ctx, P(0.6, 1), P(0.6, 2.6));
    cam.seg(ctx, P(0, 2.05), P(1.2, 2.05));
    ctx.lineWidth = 2;
    ctx.strokeStyle = frame;
    ctx.stroke();
    ctx.lineWidth = 0.8;
    fill([P(0.1, 0.15), P(1.1, 0.15), P(1.1, 0.72), P(0.1, 0.72)], frame);
    ctx.beginPath();
    for (let u = 0.18; u < 1.1; u += 0.08) cam.seg(ctx, P(u, 0.18), P(u, 0.69));
    ctx.lineWidth = 1;
    ctx.strokeStyle = mix(M.wainscot, M.fog, k);
    ctx.stroke();
  }

  // Floor: tiles in the boards' average colour, then the boards themselves near the camera
  // (a 50 m room has thousands of them; far away the fog hides their seams anyway).
  ctx.lineWidth = 0.6;
  const meanOf = (c: number) => M.wood.reduce((sum, w) => sum + w[c], 0) / M.wood.length;
  const average: RGB = [meanOf(0), meanOf(1), meanOf(2)];
  const tile = 5;
  for (let x = -half; x < half - 1e-6; x += tile)
    for (let y = -half; y < half - 1e-6; y += tile) {
      const x1 = Math.min(half, x + tile);
      const y1 = Math.min(half, y + tile);
      fill([[x, y, 0], [x1, y, 0], [x1, y1, 0], [x, y1, 0]], mix(average, M.fog, far((x + x1) / 2, (y + y1) / 2, 0)));
    }
  if (cam.z < 15) {
    const reach = 15;
    for (const p of layout.planks) {
      const mx = (p.x0 + p.x1) / 2;
      const my = (p.y0 + p.y1) / 2;
      if (Math.abs(mx - cam.x) > reach || Math.abs(my - cam.y) > reach + 1.5) continue;
      const ahead = cam.toCam(mx, my, 0);
      if (ahead.z < -1.6) continue; // behind the camera, with room for the plank's length
      const d = Math.hypot(mx - cam.x, my - cam.y, cam.z);
      const k = fog(d);
      const colour = mix(M.wood[p.tone], M.fog, k);
      fill([[p.x0, p.y0, 0], [p.x1, p.y0, 0], [p.x1, p.y1, 0], [p.x0, p.y1, 0]], colour, d < 16 ? mix(M.seam, M.fog, k) : colour);
    }
  }
  ctx.globalAlpha = 1;
}

// One audience seat (audience.ts), facing the room's centre like the players; one on the centre
// faces the front. It appears with the room.
export function drawSeat(ctx: CanvasRenderingContext2D, cam: Camera, s: { x: number; y: number }, M: Materials, shows: number, fog: Fog) {
  const toCentre = Math.hypot(s.x, s.y);
  const fx = toCentre > 0.05 ? -s.x / toCentre : 0;
  const fy = toCentre > 0.05 ? -s.y / toCentre : 1;
  const P = (dx: number, dy: number, z: number): Pt => [s.x + fy * dx + fx * dy, s.y - fx * dx + fy * dy, z]; // dx: to its right, dy: ahead
  const seat: Pt[] = [P(-0.26, -0.22, 0.45), P(0.26, -0.22, 0.45), P(0.26, 0.2, 0.45), P(-0.26, 0.2, 0.45)];
  const draw = (pts: Pt[], colour: string) => {
    ctx.beginPath();
    if (!cam.poly(ctx, pts)) return;
    ctx.fillStyle = colour;
    ctx.fill();
    ctx.strokeStyle = colour;
    ctx.stroke();
  };
  if (shows < 0.01) return;
  const k = fog(Math.hypot(s.x - cam.x, s.y - cam.y));
  const top = mix(M.seat, M.fog, k);
  const side = mix(M.seatBack, M.fog, k);
  const back: Pt[] = [P(-0.26, -0.24, 0.42), P(0.26, -0.24, 0.42), P(0.26, -0.3, 0.92), P(-0.26, -0.3, 0.92)];
  const front: Pt[] = [P(-0.26, 0.2, 0.02), P(0.26, 0.2, 0.02), P(0.26, 0.2, 0.45), P(-0.26, 0.2, 0.45)];
  ctx.globalAlpha = shows;
  ctx.lineWidth = 0.6;
  if ((cam.x - s.x) * fx + (cam.y - s.y) * fy < 0) {
    draw(front, side);
    draw(seat, top);
    draw(back, side);
  } else {
    draw(back, side);
    draw(front, side);
    draw(seat, top);
  }
  ctx.globalAlpha = 1;
}

// Dashed arcs on the floor ahead of the listener: where 3, 5 and 10 m really are.
export const distanceArcs = [3, 5, 10];

export function drawDistanceArcs(
  ctx: CanvasRenderingContext2D,
  cam: Camera,
  at: { x: number; y: number },
  yawDegrees: number,
  ink: string,
  shows: number,
  label: (text: string, x: number, y: number) => void,
) {
  const yaw = (yawDegrees * Math.PI) / 180;
  const on = (a: number, r: number): Pt => [at.x + Math.sin(a) * r, at.y + Math.cos(a) * r, 0.01];
  ctx.globalAlpha = shows;
  ctx.lineWidth = 1;
  ctx.setLineDash([4, 6]);
  ctx.strokeStyle = `rgba(${ink},0.35)`;
  for (const r of distanceArcs) {
    ctx.beginPath();
    for (let a = -60; a < 60; a += 3) cam.seg(ctx, on(yaw + (a * Math.PI) / 180, r), on(yaw + ((a + 3) * Math.PI) / 180, r));
    ctx.stroke();
  }
  ctx.setLineDash([]);
  for (const r of distanceArcs) {
    const p = cam.screen(on(yaw, r), 0.3);
    if (p) label(`${r} M`, p[0], p[1] - 8);
  }
  ctx.globalAlpha = 1;
}

// A soft vignette, so the room reads as a lit space rather than a flat picture.
export function drawVignette(ctx: CanvasRenderingContext2D, width: number, height: number, M: Materials, shows: number) {
  const g = ctx.createRadialGradient(width / 2, height * 0.55, Math.min(width, height) * 0.3, width / 2, height * 0.55, Math.max(width, height) * 0.75);
  g.addColorStop(0, `rgba(${M.shade},0)`);
  g.addColorStop(1, `rgba(${M.shade},${M.vignette})`);
  ctx.globalAlpha = shows;
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, width, height);
  ctx.globalAlpha = 1;
}
