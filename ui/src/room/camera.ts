// The camera of the stage view: straight down from high above for the map, down to the
// listener's eyes for the 3D room, and every height in between for the swoop. Pure maths, no
// DOM, so it is tested in node.
//
// World: metres, x right, y front (towards the top of the map), z up.
// Camera space: x right, y up, z depth (ahead of the camera).

export type V3 = { x: number; y: number; z: number };
export type Pt = readonly [number, number, number]; // world point
export type PathSink = { moveTo(x: number, y: number): void; lineTo(x: number, y: number): void; closePath(): void };

// Tuned on the design canvas (2026-11): what makes a 5 m distance look like 5 m.
export const view3d = {
  fovDegrees: 100, // horizontal
  panini: 0.1, // 0: straight lines (edges stretch by 1/cos²); more evens out the edges but bends lines
  squeeze: 0, // Panini's vertical squeeze: straightens horizontals back
  seatedEyes: 1.2, // metres
  standingEyes: 1.6,
  ceiling: 5,
  mapHeight: 60, // where the map is seen from
  swoopMs: 1700,
};

const near = 0.05; // metres: closer than this is clipped away

export type Shot = {
  x: number; // camera position, metres
  y: number;
  z: number;
  yaw: number; // degrees, clockwise = turned right
  pitch: number; // degrees, 0 level, -90 straight down
  focal: number; // pixels
  panini: number;
  squeeze: number;
  cx: number; // where straight ahead lands on the screen, pixels
  cy: number;
};

export class Camera {
  readonly x: number;
  readonly y: number;
  readonly z: number;
  readonly yaw: number; // radians
  private readonly cyaw: number;
  private readonly syaw: number;
  private readonly cp: number;
  private readonly sp: number;

  constructor(readonly shot: Shot) {
    this.x = shot.x;
    this.y = shot.y;
    this.z = shot.z;
    this.yaw = (shot.yaw * Math.PI) / 180;
    const pitch = (shot.pitch * Math.PI) / 180;
    this.cyaw = Math.cos(this.yaw);
    this.syaw = Math.sin(this.yaw);
    this.cp = Math.cos(pitch);
    this.sp = Math.sin(pitch);
  }

  // Straight down with plain perspective: the map, where a metre is the same everywhere on the
  // floor.
  get flat() {
    return this.shot.pitch === -90 && this.shot.panini === 0 && this.shot.squeeze === 0;
  }

  toCam(px: number, py: number, pz: number): V3 {
    const dx = px - this.x;
    const dy = py - this.y;
    const dz = pz - this.z;
    const right = dx * this.cyaw - dy * this.syaw;
    const fwd = dx * this.syaw + dy * this.cyaw;
    return { x: right, y: -fwd * this.sp + dz * this.cp, z: fwd * this.cp + dz * this.sp };
  }

  // General Panini (as in Unreal's PaniniProjection): d bends the view so wide angles keep their
  // proportions at the edges and verticals stay vertical; the squeeze straightens horizontals
  // back. d = 0 and squeeze = 0 is plain rectilinear perspective. Only for points ahead (z > 0).
  project(c: V3): [number, number] {
    const { focal, panini: d, squeeze, cx, cy } = this.shot;
    const r = Math.hypot(c.x, c.z);
    const cosA = c.z / r;
    const s = (d + 1) / (d + cosA);
    const t = c.y / r;
    return [cx + (focal * s * c.x) / r, cy - focal * s * (t + (t / cosA - t) * squeeze)];
  }

  // Pixels per metre at a point (across the view).
  scaleAt(c: V3) {
    const d = this.shot.panini;
    const r = Math.hypot(c.x, c.z);
    return (this.shot.focal * (d + 1)) / (d + c.z / r) / r;
  }

  // A world point on the screen, or undefined when it is behind (or too near) the camera.
  screen(p: Pt, minDepth = near): [number, number] | undefined {
    const c = this.toCam(p[0], p[1], p[2]);
    return c.z >= minDepth ? this.project(c) : undefined;
  }

  // Under Panini a straight edge bends, so long edges are drawn in pieces of about 2 degrees;
  // otherwise a wall shows corners instead of a smooth curve.
  pieces(a: V3, b: V3) {
    if (this.shot.panini < 0.001 && this.shot.squeeze < 0.001) return 1;
    const turn = Math.abs(Math.atan2(a.x, a.z) - Math.atan2(b.x, b.z));
    return Math.min(32, Math.max(1, Math.ceil(turn / 0.035)));
  }

  // A polygon clipped to the near plane, as a closed path; false when nothing of it is ahead.
  poly(path: PathSink, pts: readonly Pt[]) {
    const cam = pts.map((p) => this.toCam(p[0], p[1], p[2]));
    const out: V3[] = [];
    for (let i = 0; i < cam.length; i++) {
      const a = cam[i];
      const b = cam[(i + 1) % cam.length];
      if (a.z >= near) out.push(a);
      if (a.z >= near !== b.z >= near) out.push(lerp(a, b, (near - a.z) / (b.z - a.z)));
    }
    if (out.length < 3) return false;

    for (let i = 0; i < out.length; i++) {
      const a = out[i];
      const b = out[(i + 1) % out.length];
      const n = this.pieces(a, b);
      for (let j = 0; j < n; j++) {
        const [x, y] = this.project(j ? lerp(a, b, j / n) : a);
        if (i === 0 && j === 0) path.moveTo(x, y);
        else path.lineTo(x, y);
      }
    }
    path.closePath();
    return true;
  }

  // A segment clipped to the near plane, added to the path. `snap` rounds to pixel centres
  // (crisp hairlines on the flat map).
  seg(path: Omit<PathSink, "closePath">, a: Pt, b: Pt, snap?: (v: number) => number) {
    let A = this.toCam(a[0], a[1], a[2]);
    let B = this.toCam(b[0], b[1], b[2]);
    if (A.z < near && B.z < near) return;
    if (A.z < near) A = lerp(A, B, (near - A.z) / (B.z - A.z));
    else if (B.z < near) B = lerp(B, A, (near - B.z) / (A.z - B.z));
    const n = this.pieces(A, B);
    const to = (c: V3, first: boolean) => {
      const [x, y] = this.project(c);
      const sx = snap ? snap(x) : x;
      const sy = snap ? snap(y) : y;
      if (first) path.moveTo(sx, sy);
      else path.lineTo(sx, sy);
    };
    to(A, true);
    for (let j = 1; j <= n; j++) to(j === n ? B : lerp(A, B, j / n), false);
  }
}

const lerp = (a: V3, b: V3, t: number): V3 => ({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t });

// The camera `amount` of the way from the map (0) to the listener's eyes (1). The map is seen
// straight down from above the room's centre with `scale` pixels per metre, centred at
// (mapX, mapY); the eyes look along `yaw`/`pitch` from the listener, centred in the view.
// Height and focal length change geometrically, so the floor's scale glides evenly.
export function swoop(o: {
  amount: number;
  width: number;
  height: number;
  map: { x: number; y: number; scale: number };
  listener: { x: number; y: number };
  eyes: number;
  yaw: number;
  pitch: number;
}): Camera {
  const e = o.amount;
  const high = view3d.mapHeight;
  const halfFov = ((view3d.fovDegrees / 2) * Math.PI) / 180;
  const d = view3d.panini;
  const focalTop = o.map.scale * high;
  // The horizontal field of view fills the width (Panini widens what a focal length shows).
  const focalEyes = o.width / 2 / (((d + 1) * Math.sin(halfFov)) / (d + Math.cos(halfFov)));
  return new Camera({
    x: o.listener.x * e,
    y: o.listener.y * e,
    z: high * Math.pow(o.eyes / high, e),
    yaw: o.yaw * e,
    pitch: -90 + (o.pitch + 90) * e,
    focal: focalTop * Math.pow(focalEyes / focalTop, e),
    panini: d * e,
    squeeze: view3d.squeeze * e,
    cx: o.map.x + (o.width / 2 - o.map.x) * e,
    cy: o.map.y + (o.height / 2 - o.map.y) * e,
  });
}

// One step of walking: forward/back and sideways relative to where the listener faces, at
// 1.4 m/s (3 m/s faster), kept 0.3 m from the walls.
export function walk(
  at: { x: number; y: number },
  input: { forward: number; right: number; faster: boolean },
  yawDegrees: number,
  seconds: number,
  room: number,
) {
  const diagonal = input.forward !== 0 && input.right !== 0 ? Math.SQRT1_2 : 1; // not faster at an angle
  const step = (input.faster ? 3 : 1.4) * seconds * diagonal;
  const yaw = (yawDegrees * Math.PI) / 180;
  const limit = room / 2 - 0.3;
  const clamp = (v: number) => Math.max(-limit, Math.min(limit, v));
  return {
    x: clamp(at.x + (Math.sin(yaw) * input.forward + Math.cos(yaw) * input.right) * step),
    y: clamp(at.y + (Math.cos(yaw) * input.forward - Math.sin(yaw) * input.right) * step),
  };
}
