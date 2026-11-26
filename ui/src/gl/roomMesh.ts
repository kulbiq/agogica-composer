// The 3D room as triangles and lines in world space (metres), built once per room size and
// look, drawn by the GPU with the camera in a shader (gl/world.ts). Fog is added there too, per
// pixel, so the colours here are the materials as they are. Pure data: tested in node.
//
// Faces, per vertex: x, y, z, r, g, b (0..1), u, v, kind. kind 1 is a floorboard: u runs across
// it 0..1 and v along it 0..1, so the shader can draw its seams and fade it to the boards'
// average colour where boards get too small to see.
// Lines, per vertex: ax, ay, az, bx, by, bz, side (±1), end (0 a, 1 b), width (CSS px), r, g, b, a.
import { view3d, type Pt } from "../room/camera";
import { onWall, type Layout, type Materials, type RGB, type Wall } from "../room/scene";

export const faceStride = 9;
export const lineStride = 13;

// Long faces are cut into pieces this size: the lens bends straight edges (Panini), and only
// vertices are bent, so a wall needs vertices along it to curve smoothly.
const piece = 0.5;

class Floats {
  data: Float32Array;
  length = 0;
  constructor(size = 1 << 12) {
    this.data = new Float32Array(size);
  }
  push(...values: number[]) {
    if (this.length + values.length > this.data.length) {
      let size = this.data.length * 2;
      while (size < this.length + values.length) size *= 2;
      const grown = new Float32Array(size);
      grown.set(this.data);
      this.data = grown;
    }
    this.data.set(values, this.length);
    this.length += values.length;
  }
  done() {
    return this.data.slice(0, this.length);
  }
}

const unit = (c: RGB): RGB => [c[0] / 255, c[1] / 255, c[2] / 255];
const lerp3 = (a: Pt, b: Pt, t: number): Pt => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];

// A flat quad a b c d (in order round its edge), cut into nu x nv pieces, as triangles.
// (u, v) run 0..1 across it for floorboards.
export function pushQuad(out: Floats, a: Pt, b: Pt, c: Pt, d: Pt, colour: RGB, kind = 0, nu?: number, nv?: number) {
  const across = Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
  const along = Math.hypot(d[0] - a[0], d[1] - a[1], d[2] - a[2]);
  const n = nu ?? Math.max(1, Math.ceil(across / piece));
  const m = nv ?? Math.max(1, Math.ceil(along / piece));
  const [r, g, bl] = unit(colour);
  const at = (i: number, j: number): Pt => lerp3(lerp3(a, b, i / n), lerp3(d, c, i / n), j / m);
  for (let i = 0; i < n; i++)
    for (let j = 0; j < m; j++) {
      const p00 = at(i, j), p10 = at(i + 1, j), p11 = at(i + 1, j + 1), p01 = at(i, j + 1);
      const v = (p: Pt, u: number, w: number) => out.push(p[0], p[1], p[2], r, g, bl, u, w, kind);
      v(p00, i / n, j / m); v(p10, (i + 1) / n, j / m); v(p11, (i + 1) / n, (j + 1) / m);
      v(p00, i / n, j / m); v(p11, (i + 1) / n, (j + 1) / m); v(p01, i / n, (j + 1) / m);
    }
}

// A line from a to b, `width` CSS px wide whatever the distance, in pieces for the lens.
export function pushLine(out: Floats, a: Pt, b: Pt, width: number, colour: RGB, alpha = 1) {
  const n = Math.max(1, Math.ceil(Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]) / piece));
  const [r, g, bl] = unit(colour);
  for (let i = 0; i < n; i++) {
    const p = lerp3(a, b, i / n);
    const q = lerp3(a, b, (i + 1) / n);
    const v = (side: number, end: number) => out.push(p[0], p[1], p[2], q[0], q[1], q[2], side, end, width, r, g, bl, alpha);
    v(1, 0); v(1, 1); v(-1, 1);
    v(1, 0); v(-1, 1); v(-1, 0);
  }
}

export type RoomMesh = { faces: Float32Array; ceiling: Float32Array; lines: Float32Array };

const doorWidth = 0.9;

// Ceiling, walls, doors, windows and floor: what scene.ts drew face by face, in the same places.
export function buildRoom(layout: Layout, M: Materials): RoomMesh {
  const half = layout.room / 2;
  const ceiling = view3d.ceiling;
  const faces = new Floats(1 << 16);
  const top = new Floats();
  const lines = new Floats();

  // Ceiling with beams.
  const tile = Math.max(2.5, layout.room / 16);
  for (let x = -half; x < half - 1e-6; x += tile)
    for (let y = -half; y < half - 1e-6; y += tile) {
      const x1 = Math.min(half, x + tile);
      const y1 = Math.min(half, y + tile);
      pushQuad(top, [x, y, ceiling], [x1, y, ceiling], [x1, y1, ceiling], [x, y1, ceiling], M.ceiling);
      const z = ceiling - 0.02;
      pushQuad(top, [x, y, z], [x + 0.3, y, z], [x + 0.3, y1, z], [x, y1, z], M.beam, 0, 1);
    }

  // Walls: panels 2 m wide, a wainscot up to 1.05 m, a skirting board; the rail and the panel
  // joints 1.2 px, a hair off the wall so they never sink into it.
  const panel = 2;
  for (const wall of ["front", "back", "right", "left"] as const satisfies readonly Wall[])
    for (let i = 0, u0 = -half; u0 < half - 1e-6; i++, u0 += panel) {
      const u1 = Math.min(half, u0 + panel);
      const P = (u: number, z: number, inset = 0) => onWall(wall, half, u, z, inset);
      pushQuad(faces, P(u0, 1.05), P(u1, 1.05), P(u1, ceiling), P(u0, ceiling), M.wall[i % 2]);
      pushQuad(faces, P(u0, 0.14), P(u1, 0.14), P(u1, 1.05), P(u0, 1.05), M.wainscot);
      pushQuad(faces, P(u0, 0), P(u1, 0), P(u1, 0.14), P(u0, 0.14), M.skirting);
      pushLine(lines, P(u0, 1.05, 0.005), P(u1, 1.05, 0.005), 1.2, M.skirting);
      pushLine(lines, P(u0, 0.14, 0.005), P(u0, 1.05, 0.005), 1.2, M.skirting);
    }

  // Doors, 0.9 x 2.1 m, in a frame.
  for (const d of layout.doors) {
    const P = (u: number, z: number, inset: number) => onWall(d.wall, half, d.at + u, z, inset);
    pushQuad(faces, P(-0.08, 0, 0.02), P(doorWidth + 0.08, 0, 0.02), P(doorWidth + 0.08, 2.18, 0.02), P(-0.08, 2.18, 0.02), M.skirting);
    pushQuad(faces, P(0, 0, 0.025), P(doorWidth, 0, 0.025), P(doorWidth, 2.1, 0.025), P(0, 2.1, 0.025), M.door);
  }

  // Windows 1.2 x 1.6 m, sill at 0.93 m, a radiator under each.
  for (const w of layout.windows) {
    const P = (u: number, z: number, inset: number) => onWall(w.wall, half, w.at + u, z, inset);
    pushQuad(faces, P(-0.07, 0.93, 0.02), P(1.27, 0.93, 0.02), P(1.27, 2.67, 0.02), P(-0.07, 2.67, 0.02), M.skirting);
    pushQuad(faces, P(0, 1, 0.025), P(1.2, 1, 0.025), P(1.2, 2.6, 0.025), P(0, 2.6, 0.025), M.glass);
    pushLine(lines, P(0.6, 1, 0.03), P(0.6, 2.6, 0.03), 2, M.skirting);
    pushLine(lines, P(0, 2.05, 0.03), P(1.2, 2.05, 0.03), 2, M.skirting);
    pushQuad(faces, P(0.1, 0.15, 0.02), P(1.1, 0.15, 0.02), P(1.1, 0.72, 0.02), P(0.1, 0.72, 0.02), M.skirting);
    for (let u = 0.18; u < 1.1; u += 0.08) pushLine(lines, P(u, 0.18, 0.03), P(u, 0.69, 0.03), 1, M.wainscot);
  }

  // Floorboards, every one of them (the shader shows the far ones as their average colour).
  // Cut along their length so the lens can bend them; u and v stay the whole board's.
  for (const p of layout.planks) {
    const steps = Math.max(1, Math.ceil((p.y1 - p.y0) / 1));
    pushQuad(faces, [p.x0, p.y0, 0], [p.x1, p.y0, 0], [p.x1, p.y1, 0], [p.x0, p.y1, 0], M.wood[p.tone], 1, 1, steps);
  }

  return { faces: faces.done(), ceiling: top.done(), lines: lines.done() };
}

// The boards' average colour: what they look like from far away.
export function averageWood(M: Materials): RGB {
  const mean = (c: number) => M.wood.reduce((sum, w) => sum + w[c], 0) / M.wood.length;
  return [mean(0), mean(1), mean(2)];
}

// Audience seats (audience.ts), each facing the room's centre like the players (one on the
// centre faces the front): seat, back and front panel, as scene.ts drew them.
export function buildSeats(seats: readonly { x: number; y: number }[], M: Materials): Float32Array {
  const out = new Floats(Math.max(64, seats.length * 3 * 6 * faceStride));
  for (const s of seats) {
    const toCentre = Math.hypot(s.x, s.y);
    const fx = toCentre > 0.05 ? -s.x / toCentre : 0;
    const fy = toCentre > 0.05 ? -s.y / toCentre : 1;
    const P = (dx: number, dy: number, z: number): Pt => [s.x + fy * dx + fx * dy, s.y - fx * dx + fy * dy, z]; // dx: to its right, dy: ahead
    pushQuad(out, P(-0.26, 0.2, 0.02), P(0.26, 0.2, 0.02), P(0.26, 0.2, 0.45), P(-0.26, 0.2, 0.45), M.seatBack, 0, 1, 1);
    pushQuad(out, P(-0.26, -0.22, 0.45), P(0.26, -0.22, 0.45), P(0.26, 0.2, 0.45), P(-0.26, 0.2, 0.45), M.seat, 0, 1, 1);
    pushQuad(out, P(-0.26, -0.24, 0.42), P(0.26, -0.24, 0.42), P(0.26, -0.3, 0.92), P(-0.26, -0.3, 0.92), M.seatBack, 0, 1, 1);
  }
  return out.done();
}

export { Floats };
