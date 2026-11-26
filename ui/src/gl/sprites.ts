// The stage's overlay for WebGL: badges, names, labels, the listener's arrow and the minimap,
// drawn in order by one program in one call (gl/world.ts).
//
// Text and the map's badges are sprites: drawn once with the 2D canvas into a small scratch
// canvas, copied into one big texture (the atlas), then quads sampling it one texel per pixel,
// on whole device pixels, so they look exactly as the canvas drew them. In the room a badge
// changes size with every step, so there it is put together instead: its disc and outline cut
// out in the shader, its icon from a sheet of large icons (mipmapped, so any size is smooth).
import { drawIcon, iconKeys, type IconKey } from "../icons";
import type { RGBA } from "./tris";

export type Sprite = { x: number; y: number; w: number; h: number; ox: number; oy: number }; // device px; (ox, oy): the anchor inside it

// Per vertex: x, y (CSS px), u, v, mode, r, g, b, a, circle centre x, y, outer and inner radius.
// mode 0: an atlas sprite (premultiplied, times a); 1: an icon from the sheet, in (r, g, b, a);
// 2: a solid colour, cut to the circle when its outer radius is above 0.
export const overlayStride = 13;

const atlasSize = 2048;

export class SpriteAtlas {
  readonly texture: WebGLTexture;
  readonly icons: WebGLTexture;
  private entries = new Map<string, Sprite>();
  private cursorX = 0;
  private cursorY = 0;
  private row = 0;
  // Changes when the atlas was cleared to make room: sprites asked for earlier are gone.
  generation = 0;
  private scratch: CanvasRenderingContext2D;

  // `page`: where the scratch canvas lives, hidden. Canvas text takes its smoothing from the
  // page's CSS; one outside the page draws the same letters slightly differently.
  constructor(private gl: WebGLRenderingContext, page: HTMLElement) {
    const canvas = document.createElement("canvas");
    canvas.hidden = true;
    canvas.setAttribute("aria-hidden", "true");
    page.after(canvas);
    this.scratch = canvas.getContext("2d")!;
    this.texture = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, atlasSize, atlasSize, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    this.icons = this.iconSheet();
  }

  // A 2D context for measuring text (the scratch canvas's).
  get measure() {
    return this.scratch;
  }

  dispose() {
    this.scratch.canvas.remove();
    this.gl.deleteTexture(this.texture);
    this.gl.deleteTexture(this.icons);
  }

  // The sprite for `key`, drawn by `draw` the first time: a canvas `width` x `height` CSS px,
  // its anchor (ax, ay) CSS px from the corner, at `dpr` device pixels per CSS pixel. `draw`
  // draws with the anchor at (0, 0).
  get(key: string, width: number, height: number, ax: number, ay: number, dpr: number, draw: (ctx: CanvasRenderingContext2D) => void): Sprite {
    const kept = this.entries.get(key);
    if (kept) return kept;
    const ox = Math.ceil(ax * dpr);
    const oy = Math.ceil(ay * dpr);
    const w = Math.min(atlasSize, Math.ceil(width * dpr) + 2);
    const h = Math.min(atlasSize, Math.ceil(height * dpr) + 2);
    if (this.cursorX + w > atlasSize) {
      this.cursorX = 0;
      this.cursorY += this.row;
      this.row = 0;
    }
    if (this.cursorY + h > atlasSize) {
      // Full: start again (the painter asks for the frame's sprites once more).
      this.entries.clear();
      this.cursorX = this.cursorY = this.row = 0;
      this.generation++;
    }
    const s: Sprite = { x: this.cursorX, y: this.cursorY, w, h, ox, oy };
    this.cursorX += w;
    this.row = Math.max(this.row, h);

    const ctx = this.scratch;
    ctx.canvas.width = w; // also clears it
    ctx.canvas.height = h;
    ctx.setTransform(dpr, 0, 0, dpr, ox, oy);
    draw(ctx);
    const { gl } = this;
    gl.bindTexture(gl.TEXTURE_2D, this.texture);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, s.x, s.y, gl.RGBA, gl.UNSIGNED_BYTE, ctx.canvas);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    this.entries.set(key, s);
    return s;
  }

  // Every icon once, large and white, in a 4 x 4 grid; mipmapped so it shrinks smoothly.
  private iconSheet() {
    const { gl } = this;
    const ctx = this.scratch;
    ctx.canvas.width = sheetSize;
    ctx.canvas.height = sheetSize;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    iconKeys.forEach((key, i) => {
      const [cx, cy] = iconCell(i);
      drawIcon(ctx, key, cx, cy, iconDrawn, "#fff");
    });
    const t = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, ctx.canvas);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.generateMipmap(gl.TEXTURE_2D);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return t;
  }
}

// The icon sheet: cells of 128 px, the icon drawn 104 px across in each (room for mipmaps to
// shrink without bleeding into the next).
const sheetSize = 512;
const cell = 128;
const iconDrawn = 104;
const iconCell = (i: number): [number, number] => [(i % 4) * cell + cell / 2, Math.floor(i / 4) * cell + cell / 2];

// The overlay's triangles, in order.
export class OverlayQuads {
  data = new Float32Array(1 << 13);
  length = 0;

  clear() {
    this.length = 0;
  }

  get vertices() {
    return this.length / overlayStride;
  }

  private reserve(n: number) {
    if (this.length + n <= this.data.length) return;
    const grown = new Float32Array(Math.max(this.data.length * 2, this.length + n));
    grown.set(this.data.subarray(0, this.length));
    this.data = grown;
  }

  private vertex(x: number, y: number, u: number, v: number, mode: number, r: number, g: number, b: number, a: number, cx = 0, cy = 0, outer = 0, inner = 0) {
    this.reserve(overlayStride);
    const d = this.data;
    let i = this.length;
    d[i++] = x; d[i++] = y; d[i++] = u; d[i++] = v; d[i++] = mode;
    d[i++] = r; d[i++] = g; d[i++] = b; d[i++] = a;
    d[i++] = cx; d[i++] = cy; d[i++] = outer; d[i++] = inner;
    this.length = i;
  }

  private quad(x0: number, y0: number, x1: number, y1: number, u0: number, v0: number, u1: number, v1: number, mode: number, c: readonly number[], a: number, cx = 0, cy = 0, outer = 0, inner = 0) {
    const v = (x: number, y: number, u: number, w: number) => this.vertex(x, y, u, w, mode, c[0], c[1], c[2], a, cx, cy, outer, inner);
    v(x0, y0, u0, v0); v(x1, y0, u1, v0); v(x1, y1, u1, v1);
    v(x0, y0, u0, v0); v(x1, y1, u1, v1); v(x0, y1, u0, v1);
  }

  // The sprite with its anchor at (x, y) CSS px (rounded to a device pixel), at `alpha`.
  sprite(s: Sprite, x: number, y: number, alpha: number, dpr: number) {
    if (alpha <= 0) return;
    const left = (Math.round(x * dpr) - s.ox) / dpr;
    const top = (Math.round(y * dpr) - s.oy) / dpr;
    this.quad(left, top, left + s.w / dpr, top + s.h / dpr, s.x / atlasSize, s.y / atlasSize, (s.x + s.w) / atlasSize, (s.y + s.h) / atlasSize, 0, [0, 0, 0], Math.min(1, alpha));
  }

  // An icon `size` px across centred at (x, y), in colour c (0..255) at alpha a.
  icon(key: IconKey, x: number, y: number, size: number, c: RGBA) {
    const [cx, cy] = iconCell(iconKeys.indexOf(key));
    const half = (size / 2) * (cell / iconDrawn);
    const u0 = (cx - cell / 2) / sheetSize, v0 = (cy - cell / 2) / sheetSize;
    const u1 = (cx + cell / 2) / sheetSize, v1 = (cy + cell / 2) / sheetSize;
    this.quad(x - half, y - half, x + half, y + half, u0, v0, u1, v1, 1, [c[0] / 255, c[1] / 255, c[2] / 255], c[3]);
  }

  triangle(ax: number, ay: number, bx: number, by: number, cx: number, cy: number, c: RGBA) {
    if (c[3] <= 0) return;
    const r = c[0] / 255, g = c[1] / 255, b = c[2] / 255;
    this.vertex(ax, ay, 0, 0, 2, r, g, b, c[3]);
    this.vertex(bx, by, 0, 0, 2, r, g, b, c[3]);
    this.vertex(cx, cy, 0, 0, 2, r, g, b, c[3]);
  }

  // A convex polygon, or one every corner of which its first point sees (fanned from there).
  polygon(xs: readonly number[], ys: readonly number[], c: RGBA) {
    for (let i = 1; i + 1 < xs.length; i++) this.triangle(xs[0], ys[0], xs[i], ys[i], xs[i + 1], ys[i + 1], c);
  }

  rect(x0: number, y0: number, x1: number, y1: number, c: RGBA) {
    this.polygon([x0, x1, x1, x0], [y0, y0, y1, y1], c);
  }

  disc(x: number, y: number, r: number, c: RGBA) {
    if (r > 0) this.circle(x, y, r, 0, c);
  }

  // A circle's outline `width` px wide, centred on radius r (as the canvas strokes an arc).
  ring(x: number, y: number, r: number, width: number, c: RGBA) {
    this.circle(x, y, r + width / 2, Math.max(1e-3, r - width / 2), c);
  }

  private circle(x: number, y: number, outer: number, inner: number, c: RGBA) {
    if (c[3] <= 0) return;
    const e = outer + 1;
    this.quad(x - e, y - e, x + e, y + e, 0, 0, 0, 0, 2, [c[0] / 255, c[1] / 255, c[2] / 255], c[3], x, y, outer, inner);
  }
}
