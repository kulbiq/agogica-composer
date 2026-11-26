// The map's own maths for zooming and for moving or picking several nodes at once. Pure, so
// it is tested in node.
import type { Point } from "./bridge";
import type { Hit, NodeKey } from "./stagePainter";

// How far the map is zoomed in (1: the whole room) and which point of the room is at the
// centre of the view, in metres.
export type Zoom = { k: number; x: number; y: number };
export const fitted: Zoom = { k: 1, x: 0, y: 0 };
export const maxZoom = 8;

// The view centre may wander only as far as keeps the room filling at least half the view:
// none at all when the whole room is shown.
const keepIn = (z: Zoom, half: number): Zoom => {
  const limit = half * (1 - 1 / z.k);
  const clamp = (v: number) => Math.max(-limit, Math.min(limit, v)) + 0; // + 0: never -0
  return { k: z.k, x: clamp(z.x), y: clamp(z.y) };
};

// Zoom by `factor`, keeping the room point `at` (under the pointer) where it is on screen.
export function zoomAt(z: Zoom, factor: number, at: Point, half: number): Zoom {
  const k = Math.max(1, Math.min(maxZoom, z.k * factor));
  const keep = z.k / k;
  return keepIn({ k, x: at.x - (at.x - z.x) * keep, y: at.y - (at.y - z.y) * keep }, half);
}

// Move the view by (dx, dy) metres: the room follows the pointer.
export const pan = (z: Zoom, dx: number, dy: number, half: number) => keepIn({ k: z.k, x: z.x - dx, y: z.y - dy }, half);

// Every node moved by the same vector, each one kept inside the room; to the centimetre, like
// every position the map writes.
export function moveAll(starts: ReadonlyMap<NodeKey, Point>, by: Point, half: number) {
  const place = (v: number) => Math.round(Math.max(-half, Math.min(half, v)) * 100) / 100;
  const out = new Map<NodeKey, Point>();
  for (const [key, p] of starts) out.set(key, { x: place(p.x + by.x), y: place(p.y + by.y) });
  return out;
}

// The players whose badges are inside the rectangle between two screen points.
export function within(hits: readonly Hit[], a: { x: number; y: number }, b: { x: number; y: number }): number[] {
  const [x0, x1] = [Math.min(a.x, b.x), Math.max(a.x, b.x)];
  const [y0, y1] = [Math.min(a.y, b.y), Math.max(a.y, b.y)];
  return hits.filter((h) => h.key !== "listener" && h.x >= x0 && h.x <= x1 && h.y >= y0 && h.y <= y1).map((h) => h.key as number);
}

// Adds a node to the selection, or takes it out (the last one added leads).
export const toggle = (selection: readonly NodeKey[], key: NodeKey) =>
  selection.includes(key) ? selection.filter((k) => k !== key) : [...selection, key];
