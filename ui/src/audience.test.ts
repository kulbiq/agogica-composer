import { expect, test } from "vitest";
import stateRules from "../../Source/stage/StateRules.h?raw";
import { cellOf, decodeAudience, emptyAudience, encodeAudience, hasSeat, seatPitch, seatsIn, setSeat } from "./audience";

test("seats survive saving; none at all saves nothing; anything else reads as no audience", () => {
  const seats = emptyAudience();
  setSeat(seats, 0, -4, true);
  setSeat(seats, 32, 32, true);
  setSeat(seats, 33, 0, true); // outside the grid: ignored
  const text = encodeAudience(seats);
  const back = decodeAudience(text);
  expect(hasSeat(back, 0, -4)).toBe(true);
  expect(hasSeat(back, 32, 32)).toBe(true);
  expect(hasSeat(back, 1, -4)).toBe(false);
  setSeat(back, 0, -4, false);
  expect(hasSeat(back, 0, -4)).toBe(false);

  expect(encodeAudience(emptyAudience())).toBe("");
  expect(seatsIn(decodeAudience("not base64!"), 50)).toEqual([]);
  expect(seatsIn(decodeAudience(btoa("short")), 50)).toEqual([]);
});

test("C++ accepts exactly what the page saves (StateRules.h, audienceLength)", () => {
  const seats = emptyAudience();
  setSeat(seats, 1, 1, true);
  const length = Number(/audienceLength = (\d+);/.exec(stateRules)?.[1]);
  expect(encodeAudience(seats).length).toBe(length);
});

test("a smaller room hides the seats beyond its walls and keeps them", () => {
  const seats = emptyAudience();
  setSeat(seats, 0, 0, true);
  setSeat(seats, 10, 0, true); // 7.5 m out
  expect(seatsIn(seats, 10)).toEqual([{ x: 0, y: 0 }]);
  expect(seatsIn(seats, 20)).toEqual([
    { x: 0, y: 0 },
    { x: 10 * seatPitch, y: 0 },
  ]);
  expect(cellOf({ x: 0.4, y: -0.3 })).toEqual([1, 0]);
  expect(cellOf({ x: 30, y: 0 })).toBeUndefined();
});
