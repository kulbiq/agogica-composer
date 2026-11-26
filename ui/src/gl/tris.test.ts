import { describe, expect, it } from "vitest";
import { PathCollector, Tris, trisStride } from "./tris";

const ink = [10, 20, 30, 0.5] as const;
const vertex = (t: Tris, i: number) => Array.from(t.data.subarray(i * trisStride, (i + 1) * trisStride));

describe("Tris", () => {
  it("draws a circle as one quad cut out in the shader", () => {
    const t = new Tris();
    t.disc(50, 40, 10, ink);
    expect(t.vertices).toBe(6);
    const [x, y, depth, r, g, b, a, cx, cy, outer, inner] = vertex(t, 0);
    expect([x, y]).toEqual([39, 29]); // the quad reaches 1 px past the edge for its smoothing
    expect([depth, cx, cy, outer, inner]).toEqual([0, 50, 40, 10, 0]);
    expect([r, g, b, a].map((v) => +v.toFixed(4))).toEqual([+(10 / 255).toFixed(4), +(20 / 255).toFixed(4), +(30 / 255).toFixed(4), 0.5]);
  });

  it("draws a ring centred on its radius, as the canvas strokes an arc", () => {
    const t = new Tris();
    t.ring(0, 0, 14, 1, ink);
    const [, , , , , , , , , outer, inner] = vertex(t, 0);
    expect([outer, inner]).toEqual([14.5, 13.5]);
  });

  it("draws a line as a quad `width` px across", () => {
    const t = new Tris();
    t.line(0, 0, 10, 0, 2, ink);
    expect(t.vertices).toBe(6);
    const ys = new Set(Array.from({ length: 6 }, (_, i) => vertex(t, i)[1]));
    expect([...ys].sort()).toEqual([-1, 1]);
  });

  it("leaves out what can't be seen", () => {
    const t = new Tris();
    t.disc(0, 0, 5, [0, 0, 0, 0]);
    t.line(0, 0, 0, 0, 1, ink);
    expect(t.vertices).toBe(0);
  });

  it("keeps a path's triangles as one group, drawn once where they overlap", () => {
    const t = new Tris();
    t.line(0, 0, 1, 1, 1, ink);
    t.begin();
    t.line(0, 0, 10, 0, 1, ink);
    t.line(5, -5, 5, 5, 1, ink);
    t.end();
    expect(t.groups).toEqual([{ first: 6, count: 12, once: true }]);
  });

  it("dashes along a subpath and starts the pattern again at the next", () => {
    const t = new Tris();
    const p = new PathCollector();
    p.moveTo(0, 0);
    p.lineTo(20, 0); // on 0-4, off 4-10, on 10-14, off 14-20
    p.moveTo(0, 10);
    p.lineTo(3, 10); // on 0-3
    p.stroke(t, 1, ink, [4, 6]);
    const xs = (i: number) => [0, 1, 2, 3, 4, 5].map((k) => vertex(t, i * 6 + k)[0]);
    expect(t.vertices).toBe(18);
    expect([Math.min(...xs(0)), Math.max(...xs(0))]).toEqual([0, 4]);
    expect([Math.min(...xs(1)), Math.max(...xs(1))]).toEqual([10, 14]);
    expect([Math.min(...xs(2)), Math.max(...xs(2))]).toEqual([0, 3]);
  });
});
