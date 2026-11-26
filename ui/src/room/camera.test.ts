import { expect, test } from "vitest";
import { Camera, swoop, view3d, walk, type PathSink, type Shot } from "./camera";

const map = { x: 480, y: 300, scale: 25 };
const at = (amount: number, extra: Partial<Parameters<typeof swoop>[0]> = {}) =>
  swoop({ amount, width: 960, height: 600, map, listener: { x: 2, y: -3 }, eyes: 1.2, yaw: 30, pitch: 0, ...extra });

class Recorder implements PathSink {
  points: [number, number][] = [];
  closed = false;
  moveTo(x: number, y: number) { this.points.push([x, y]); }
  lineTo(x: number, y: number) { this.points.push([x, y]); }
  closePath() { this.closed = true; }
}

test("the map end of the swoop is the flat map: a metre is `scale` pixels everywhere on the floor", () => {
  const cam = at(0);
  expect(cam.flat).toBe(true);
  for (const [x, y] of [[0, 0], [3, 2], [-10, 10], [7.5, -9]]) {
    const [sx, sy] = cam.screen([x, y, 0])!;
    expect(sx).toBeCloseTo(map.x + x * map.scale, 9);
    expect(sy).toBeCloseTo(map.y - y * map.scale, 9);
  }
});

test("at the eyes: centred ahead, Panini 0 is a pinhole camera, verticals stay vertical", () => {
  const cam = at(1);
  expect(cam.flat).toBe(false);
  expect(cam.z).toBeCloseTo(1.2, 9);
  const ahead = (dist: number, side: number, z: number) => {
    const yaw = (30 * Math.PI) / 180;
    return [2 + Math.sin(yaw) * dist + Math.cos(yaw) * side, -3 + Math.cos(yaw) * dist - Math.sin(yaw) * side, z] as const;
  };
  const [cx, cy] = cam.screen(ahead(5, 0, 1.2))!;
  expect(cx).toBeCloseTo(480, 6);
  expect(cy).toBeCloseTo(300, 6);

  // A pole 2 m to the right: one screen x from floor to ceiling (Panini keeps verticals).
  const foot = cam.screen(ahead(5, 2, 0))!;
  const top = cam.screen(ahead(5, 2, view3d.ceiling))!;
  expect(top[0]).toBeCloseTo(foot[0], 9);
  expect(top[1]).toBeLessThan(foot[1]);

  const pinhole = new Camera({ ...cam.shot, panini: 0, squeeze: 0 });
  const p = pinhole.toCam(...ahead(5, 1, 1.2));
  expect(pinhole.project(p)[0]).toBeCloseTo(480 + (cam.shot.focal * 1) / 5, 6);
});

test("the view is 100 degrees wide at the eyes", () => {
  const cam = new Camera({ ...at(1, { yaw: 0, listener: { x: 0, y: 0 } }).shot });
  const half = (view3d.fovDegrees / 2) * (Math.PI / 180);
  const [edge] = cam.screen([Math.sin(half) * 4, Math.cos(half) * 4, 1.2])!;
  expect(edge).toBeCloseTo(960, 6);
});

test("long edges are drawn in pieces only when they can bend", () => {
  const curved = at(1);
  const a = curved.toCam(-8, 8, 0), b = curved.toCam(8, 8, 0);
  expect(curved.pieces(a, b)).toBeGreaterThan(10);
  const straight = new Camera({ ...curved.shot, panini: 0, squeeze: 0 });
  expect(straight.pieces(a, b)).toBe(1);
});

test("near-plane clipping: nothing behind the camera is drawn, a wall around it is cut", () => {
  const shot: Shot = { x: 0, y: 0, z: 1.2, yaw: 0, pitch: 0, focal: 400, panini: 0, squeeze: 0, cx: 480, cy: 300 };
  const cam = new Camera(shot);

  const behind = new Recorder();
  expect(cam.poly(behind, [[-1, -2, 0], [1, -2, 0], [1, -4, 0], [-1, -4, 0]])).toBe(false);
  expect(behind.points).toHaveLength(0);

  // Floor from 2 m behind to 3 m ahead: only its front part, all of it on screen below the horizon.
  const floor = new Recorder();
  expect(cam.poly(floor, [[-1, -2, 0], [1, -2, 0], [1, 3, 0], [-1, 3, 0]])).toBe(true);
  expect(floor.closed).toBe(true);
  expect(floor.points.length).toBeGreaterThanOrEqual(3);
  for (const [, y] of floor.points) expect(y).toBeGreaterThan(300);

  const line = new Recorder();
  cam.seg(line, [0, -5, 0], [0, 5, 0]);
  expect(line.points).toHaveLength(2);
});

test("walking: relative to the way the listener faces, kept off the walls, not faster at an angle", () => {
  const p = walk({ x: 0, y: 0 }, { forward: 1, right: 0, faster: false }, 90, 1, 20);
  expect(p.x).toBeCloseTo(1.4, 9);
  expect(p.y).toBeCloseTo(0, 9);

  const strafe = walk({ x: 0, y: 0 }, { forward: 0, right: 1, faster: false }, 0, 1, 20);
  expect(strafe.x).toBeCloseTo(1.4, 9);

  const diagonal = walk({ x: 0, y: 0 }, { forward: 1, right: 1, faster: false }, 0, 1, 20);
  expect(Math.hypot(diagonal.x, diagonal.y)).toBeCloseTo(1.4, 9);

  const wall = walk({ x: 9.6, y: 0 }, { forward: 0, right: 1, faster: true }, 0, 1, 20);
  expect(wall.x).toBeCloseTo(9.7, 9);
});
