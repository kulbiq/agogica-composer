import { expect, test } from "vitest";
import { layoutRoom, onWall } from "./scene";

test.each([5, 20, 50])("a %i m room: boards cover the floor, openings stay inside", (room) => {
  const half = room / 2;
  const layout = layoutRoom(room);

  const area = layout.planks.reduce((sum, p) => sum + (p.x1 - p.x0) * (p.y1 - p.y0), 0);
  expect(area).toBeCloseTo(room * room, 3);
  for (const p of layout.planks) expect(Math.max(Math.abs(p.x0), Math.abs(p.x1), Math.abs(p.y0), Math.abs(p.y1))).toBeLessThanOrEqual(half + 1e-9);

  // Windows and doors fit their walls and never overlap one another.
  const openings = [...layout.doors.map((d) => ({ ...d, w: 0.9 })), ...layout.windows.map((w) => ({ ...w, w: 1.2 }))];
  for (const o of openings) {
    expect(o.at).toBeGreaterThanOrEqual(-half);
    expect(o.at + o.w).toBeLessThanOrEqual(half);
    for (const other of openings)
      if (other !== o && other.wall === o.wall) expect(o.at + o.w <= other.at || other.at + other.w <= o.at).toBe(true);
  }
});

test("the same room is laid out the same way every time", () => {
  expect(layoutRoom(20)).toEqual(layoutRoom(20));
});

test("walls: u runs left to right as seen from inside", () => {
  // Facing the front wall, the right wall is ahead to the right, and so on round the room.
  expect(onWall("front", 10, 2, 0)).toEqual([2, 10, 0]);
  expect(onWall("right", 10, 2, 0)).toEqual([10, -2, 0]);
  expect(onWall("back", 10, 2, 0)).toEqual([-2, -10, 0]);
  expect(onWall("left", 10, 2, 0)).toEqual([-10, 2, 0]);
});
