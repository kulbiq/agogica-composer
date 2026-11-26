import { describe, expect, it } from "vitest";
import { fitted, maxZoom, moveAll, pan, toggle, within, zoomAt } from "./mapEdits";

describe("zoom", () => {
  it("keeps the point under the pointer in place, between the whole room and the maximum", () => {
    const z = zoomAt(fitted, 2, { x: 4, y: -2 }, 10);
    expect(z.k).toBe(2);
    // On screen a point sits at (p - centre) * k from the view's centre: 4 m stays at 4 m.
    expect((4 - z.x) * z.k).toBeCloseTo(4);
    expect((-2 - z.y) * z.k).toBeCloseTo(-2);
    expect(zoomAt(z, 100, { x: 0, y: 0 }, 10).k).toBe(maxZoom);
    expect(zoomAt(z, 0.01, { x: 4, y: -2 }, 10)).toEqual(fitted); // all the way out: the whole room, centred
  });

  it("never lets the room leave the view", () => {
    const z = pan({ k: 2, x: 0, y: 0 }, -100, 100, 10);
    expect(z).toEqual({ k: 2, x: 5, y: -5 }); // half the room still in view
    expect(pan(fitted, 3, 3, 10)).toEqual(fitted);
  });
});

describe("several nodes", () => {
  it("move together, each kept in the room", () => {
    const moved = moveAll(
      new Map([
        [1, { x: 0, y: 0 }],
        [2, { x: 9, y: 1 }],
      ]),
      { x: 2, y: 0.5 },
      10,
    );
    expect(moved.get(1)).toEqual({ x: 2, y: 0.5 });
    expect(moved.get(2)).toEqual({ x: 10, y: 1.5 });
  });

  it("are picked by a rectangle drawn either way, players only", () => {
    const hits = [
      { key: 1, x: 10, y: 10, r: 16 },
      { key: 2, x: 50, y: 10, r: 16 },
      { key: "listener" as const, x: 20, y: 20, r: 16 },
    ];
    expect(within(hits, { x: 30, y: 30 }, { x: 0, y: 0 })).toEqual([1]);
  });

  it("toggle in and out of the selection, the newest last", () => {
    expect(toggle([1, 2], 3)).toEqual([1, 2, 3]);
    expect(toggle([1, 2], 1)).toEqual([2]);
  });
});
