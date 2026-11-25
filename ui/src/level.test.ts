import { expect, test } from "vitest";
import { levelOf } from "./level";

test("a level is a fraction of the meter: empty at -60 dB, full at 0 dB and above", () => {
  expect(levelOf({ 1: -60 }, 1)).toBe(0);
  expect(levelOf({ 1: -30 }, 1)).toBe(0.5);
  expect(levelOf({ 1: 0 }, 1)).toBe(1);
  expect(levelOf({ 1: 6 }, 1)).toBe(1); // a clipping input doesn't overflow the meter
  expect(levelOf({ 1: -90 }, 1)).toBe(0);
  expect(levelOf({}, 1)).toBe(0);
});
