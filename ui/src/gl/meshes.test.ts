import { describe, expect, it } from "vitest";
import { layoutRoom, type Materials } from "../room/scene";
import { buildFigures, figureStride, partOrder } from "./figureMesh";
import { buildRoom, buildSeats, faceStride, lineStride } from "./roomMesh";

const grey = (v: number) => [v, v, v] as const;
const M: Materials = {
  wood: [200, 202, 204, 206, 208].map(grey),
  seam: grey(100),
  wall: [grey(220), grey(210)],
  wainscot: grey(180),
  skirting: grey(140),
  ceiling: grey(170),
  beam: grey(150),
  door: grey(140),
  glass: grey(225),
  seat: grey(120),
  seatBack: grey(100),
  fog: grey(234),
  shade: "0,0,0",
  shadow: 0.2,
  vignette: 0.16,
};

describe("the room's mesh", () => {
  const mesh = buildRoom(layoutRoom(20), M);

  it("holds whole vertices, every value a number", () => {
    for (const [data, stride] of [[mesh.faces, faceStride], [mesh.ceiling, faceStride], [mesh.lines, lineStride]] as const) {
      expect(data.length % stride).toBe(0);
      expect(data.length / stride % 3).toBe(0); // whole triangles
      expect(data.every(Number.isFinite)).toBe(true);
    }
  });

  it("reads back as the layout says (catches a stride out of step with what is written)", () => {
    for (let i = 0; i < mesh.lines.length; i += lineStride) {
      const [, , , , , , side, end, width, r, g, b, a] = mesh.lines.subarray(i, i + lineStride);
      expect(Math.abs(side)).toBe(1);
      expect([0, 1]).toContain(end);
      expect(width).toBeGreaterThanOrEqual(1);
      expect(width).toBeLessThanOrEqual(2);
      for (const c of [r, g, b, a]) expect(c).toBeGreaterThanOrEqual(0), expect(c).toBeLessThanOrEqual(1);
    }
    for (let i = 0; i < mesh.faces.length; i += faceStride) {
      const [x, y, z, r, , , u, v, kind] = mesh.faces.subarray(i, i + faceStride);
      expect(Math.abs(x)).toBeLessThanOrEqual(10.001);
      expect(Math.abs(y)).toBeLessThanOrEqual(10.001);
      expect(z).toBeGreaterThanOrEqual(0);
      expect(r).toBeLessThanOrEqual(1);
      if (kind === 1) {
        expect(z).toBe(0); // floorboards lie on the floor
        expect(u).toBeGreaterThanOrEqual(0), expect(u).toBeLessThanOrEqual(1);
        expect(v).toBeGreaterThanOrEqual(0), expect(v).toBeLessThanOrEqual(1);
      }
    }
  });

  it("draws every floorboard, whatever the camera", () => {
    const boards = layoutRoom(20).planks.length;
    let kinds = 0;
    for (let i = 8; i < mesh.faces.length; i += faceStride) kinds += mesh.faces[i];
    expect(kinds / 6).toBeGreaterThanOrEqual(boards); // at least two triangles each (long ones are cut)
  });

  it("gives every seat its seat, back and front", () => {
    const seats = buildSeats([{ x: 1, y: -3 }, { x: 0, y: 0 }], M);
    expect(seats.length).toBe(2 * 3 * 6 * faceStride);
  });
});

describe("the players' mesh", () => {
  const players = Array.from({ length: 64 }, (_, i) => ({ id: i, x: (i % 8) - 3.5, y: Math.floor(i / 8) + 2, selected: i === 3 }));
  const mesh = buildFigures(players);

  it("holds whole vertices, and 64 players fit 16-bit indices", () => {
    expect(mesh.data.length % figureStride).toBe(0);
    expect(mesh.data.every(Number.isFinite)).toBe(true);
    expect(mesh.data.length / figureStride).toBeLessThan(65536);
  });

  it("keeps each player's parts apart, in runs that don't overlap", () => {
    const runs = [...mesh.players.values()].flatMap((p) => [p.chairBase, p.chairBack, p.legs, p.torso, p.stand]);
    runs.push(mesh.shadows);
    runs.sort((a, b) => a[0] - b[0]);
    for (let i = 1; i < runs.length; i++) expect(runs[i][0]).toBe(runs[i - 1][0] + runs[i - 1][1]);
    for (const [, count] of runs) expect(count % 3).toBe(0);
    expect(mesh.shadows[1]).toBe(64 * 14 * 3);
  });

  it("puts the chair back behind the body seen from the front, in front of it from behind", () => {
    const parts = mesh.players.get(0)!;
    expect(partOrder(parts, true)).toEqual([parts.chairBase, parts.chairBack, parts.legs, parts.torso, parts.stand]);
    expect(partOrder(parts, false)).toEqual([parts.chairBase, parts.stand, parts.legs, parts.torso, parts.chairBack]);
  });

  it("widens each line into a quad as the shader does: two triangles over one rectangle", () => {
    // The shader puts a line's vertex at its end + side × the normal of (other end − this end).
    const [first] = mesh.players.get(0)!.chairBase;
    const at = (v: number) => {
      const d = mesh.data.subarray(v * figureStride, (v + 1) * figureStride);
      const [x, y, , kind, side, , , ox, oy] = d;
      expect(kind).toBe(1);
      const len = Math.hypot(ox - x, oy - y) || 1;
      // A chair leg is vertical: seen from above, both ends coincide, so look from the side (x, z).
      const [z, oz] = [d[2], d[9]];
      const l = Math.hypot(ox - x, oz - z) || len;
      return [x + side * (-(oz - z) / l), z + side * ((ox - x) / l)];
    };
    const area = (a: number[], b: number[], c: number[]) => ((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) / 2;
    const t1 = area(at(first), at(first + 1), at(first + 2));
    const t2 = area(at(first + 3), at(first + 4), at(first + 5));
    expect(Math.abs(t1)).toBeGreaterThan(0);
    expect(Math.sign(t1)).toBe(Math.sign(t2)); // the same way round: not folded over each other
  });

  it("is lighter for a player that isn't selected", () => {
    const alphaOf = (id: number) => mesh.data[mesh.players.get(id)!.chairBack[0] * figureStride + 12];
    expect(alphaOf(3)).toBeCloseTo(0.45);
    expect(alphaOf(0)).toBeCloseTo(0.28);
  });
});
