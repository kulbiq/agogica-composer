// Screen-space triangles for the stage's WebGL pass: what the map and the players are drawn
// with (the grid, the azimuth rose, shadows, figures, halos, rings). Positions are CSS pixels,
// as the 2D canvas had them; `depth` is the camera depth (metres) of what is being drawn, so it
// is hidden behind nearer seats and walls, or 0 for "no depth" (drawn over everything earlier).
// Circles are one quad each, cut out exactly in the shader (gl/world.ts). Pure data, no DOM:
// tested in node.
import type { PathSink } from "../room/camera";

export type RGBA = readonly [number, number, number, number]; // 0..255, alpha 0..1

// Per vertex: x, y (CSS px), depth (metres, 0: none), r, g, b, a (0..1, not premultiplied),
// then the circle it is part of: centre x, y, outer and inner radius (CSS px). An outer radius
// of 0 is a plain triangle; an inner one above 0 makes a ring.
export const trisStride = 11;

// One stroke or fill that the canvas drew as a single path: where its own triangles overlap
// (lines crossing, the pieces of a curve), the colour is laid down once. The renderer draws a
// group with the stencil buffer when `once` is set; otherwise triangles simply blend.
export type Group = { first: number; count: number; once: boolean };

export class Tris {
  data = new Float32Array(1 << 14);
  length = 0; // floats used
  depth = 0;
  groups: Group[] = [];
  private open: Group | undefined;

  clear() {
    this.length = 0;
    this.groups = [];
    this.open = undefined;
    this.depth = 0;
  }

  get vertices() {
    return this.length / trisStride;
  }

  // Everything added until `end` is one path, drawn once where it overlaps itself.
  begin() {
    this.end();
    this.open = { first: this.vertices, count: 0, once: true };
  }

  end() {
    const g = this.open;
    if (!g) return;
    g.count = this.vertices - g.first;
    if (g.count > 0) this.groups.push(g);
    this.open = undefined;
  }

  private reserve(n: number) {
    if (this.length + n <= this.data.length) return;
    let size = this.data.length * 2;
    while (size < this.length + n) size *= 2;
    const grown = new Float32Array(size);
    grown.set(this.data.subarray(0, this.length));
    this.data = grown;
  }

  private vertex(x: number, y: number, c: RGBA, cx = 0, cy = 0, outer = 0, inner = 0) {
    this.reserve(trisStride);
    const d = this.data;
    let i = this.length;
    d[i++] = x;
    d[i++] = y;
    d[i++] = this.depth;
    d[i++] = c[0] / 255;
    d[i++] = c[1] / 255;
    d[i++] = c[2] / 255;
    d[i++] = c[3];
    d[i++] = cx;
    d[i++] = cy;
    d[i++] = outer;
    d[i++] = inner;
    this.length = i;
  }

  triangle(ax: number, ay: number, bx: number, by: number, cx: number, cy: number, c: RGBA) {
    if (c[3] <= 0) return;
    this.vertex(ax, ay, c);
    this.vertex(bx, by, c);
    this.vertex(cx, cy, c);
  }

  // A convex polygon (as the canvas fills them here: faces clipped to the near plane).
  polygon(xs: readonly number[], ys: readonly number[], c: RGBA) {
    for (let i = 1; i + 1 < xs.length; i++) this.triangle(xs[0], ys[0], xs[i], ys[i], xs[i + 1], ys[i + 1], c);
  }

  // A straight line `width` px wide, butt ends (the canvas's default cap).
  line(ax: number, ay: number, bx: number, by: number, width: number, c: RGBA) {
    const dx = bx - ax;
    const dy = by - ay;
    const length = Math.hypot(dx, dy);
    if (length < 1e-6) return;
    const nx = (-dy / length) * (width / 2);
    const ny = (dx / length) * (width / 2);
    this.triangle(ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, c);
    this.triangle(ax + nx, ay + ny, bx - nx, by - ny, ax - nx, ay - ny, c);
  }

  // A filled circle: one quad, its edge cut out and smoothed per pixel by the shader.
  disc(x: number, y: number, r: number, c: RGBA) {
    if (r > 0) this.circle(x, y, r, 0, c);
  }

  // A circle's outline `width` px wide, centred on radius r.
  ring(x: number, y: number, r: number, width: number, c: RGBA) {
    this.circle(x, y, r + width / 2, Math.max(1e-3, r - width / 2), c);
  }

  private circle(x: number, y: number, outer: number, inner: number, c: RGBA) {
    if (c[3] <= 0) return;
    const e = outer + 1; // room for the smoothed edge
    const v = (px: number, py: number) => this.vertex(px, py, c, x, y, outer, inner);
    v(x - e, y - e); v(x + e, y - e); v(x + e, y + e);
    v(x - e, y - e); v(x + e, y + e); v(x - e, y + e);
  }
}

// Collects one path from Camera.poly / Camera.seg: polygons for fills, polylines for strokes.
export class PathCollector implements PathSink {
  xs: number[][] = [];
  ys: number[][] = [];
  closed: boolean[] = [];

  reset() {
    this.xs = [];
    this.ys = [];
    this.closed = [];
    return this;
  }

  moveTo(x: number, y: number) {
    this.xs.push([x]);
    this.ys.push([y]);
    this.closed.push(false);
  }

  lineTo(x: number, y: number) {
    if (!this.xs.length) return this.moveTo(x, y);
    this.xs[this.xs.length - 1].push(x);
    this.ys[this.ys.length - 1].push(y);
  }

  closePath() {
    if (this.closed.length) this.closed[this.closed.length - 1] = true;
  }

  fill(t: Tris, c: RGBA) {
    for (let i = 0; i < this.xs.length; i++) t.polygon(this.xs[i], this.ys[i], c);
  }

  // `dash`: on and off lengths in px, as the canvas's setLineDash: the pattern runs along each
  // subpath and starts again at the next one.
  stroke(t: Tris, width: number, c: RGBA, dash?: readonly number[]) {
    for (let i = 0; i < this.xs.length; i++) {
      const xs = this.xs[i].slice();
      const ys = this.ys[i].slice();
      if (this.closed[i] && xs.length > 2) {
        xs.push(xs[0]);
        ys.push(ys[0]);
      }
      if (!dash) {
        for (let j = 0; j + 1 < xs.length; j++) t.line(xs[j], ys[j], xs[j + 1], ys[j + 1], width, c);
        continue;
      }
      let k = 0; // which dash entry
      let left = dash[0]; // px left of it
      for (let j = 0; j + 1 < xs.length; j++) {
        let ax = xs[j];
        let ay = ys[j];
        const bx = xs[j + 1];
        const by = ys[j + 1];
        let length = Math.hypot(bx - ax, by - ay);
        while (length > 1e-9) {
          const step = Math.min(left, length);
          const f = step / length;
          const nx = ax + (bx - ax) * f;
          const ny = ay + (by - ay) * f;
          if (k % 2 === 0) t.line(ax, ay, nx, ny, width, c);
          ax = nx;
          ay = ny;
          length -= step;
          left -= step;
          if (left <= 1e-9) {
            k = (k + 1) % dash.length;
            left = dash[k];
          }
        }
      }
    }
  }
}

// "r,g,b" (the --ink-rgb token) as numbers.
export function parseRgb(text: string): [number, number, number] {
  const [r, g, b] = text.split(",").map((v) => Number(v.trim()));
  return [r || 0, g || 0, b || 0];
}

export const rgba = (rgb: readonly [number, number, number] | readonly number[], a: number): RGBA => [rgb[0], rgb[1], rgb[2], a];
