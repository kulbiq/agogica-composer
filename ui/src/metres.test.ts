import { expect, test } from "vitest";
import { committedMetres } from "./metres";

test("a typed number is committed in metres, inside the room, on the 1 cm grid", () => {
  expect(committedMetres("1.5", 20)).toBe(1.5);
  expect(committedMetres(" -2.456 ", 20)).toBe(-2.46);
  expect(committedMetres("30", 20)).toBe(10);
  expect(committedMetres("-30", 20)).toBe(-10);
});

test("text that isn't a number commits nothing", () => {
  expect(committedMetres("", 20)).toBeUndefined();
  expect(committedMetres("abc", 20)).toBeUndefined();
});
