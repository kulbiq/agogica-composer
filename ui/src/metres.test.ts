import { expect, test } from "vitest";
import { committedMetres, typedMetres } from "./metres";

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

test("leaving the field without typing commits nothing", () => {
  // The field shows the place inside the room (5.00) while the track remembers 12 m for when
  // the room grows again: tabbing through must not replace 12 with 5.
  expect(committedMetres("5.00", 10, "5.00")).toBeUndefined();
  expect(committedMetres("4", 10, "5.00")).toBe(4);
});

test("a decimal comma and a typographic minus are numbers; other notations are not", () => {
  expect(committedMetres("1,5", 20)).toBe(1.5);
  expect(committedMetres("−2,25", 20)).toBe(-2.25);
  expect(committedMetres("0x10", 20)).toBeUndefined();
  expect(committedMetres("1e3", 20)).toBeUndefined();
  expect(committedMetres("1,5,2", 20)).toBeUndefined();
});

test("typed text is told apart from a number while typing", () => {
  expect(typedMetres("abc")).toBeUndefined();
  expect(typedMetres("-")).toBeUndefined();
  expect(typedMetres(".5")).toBe(0.5);
  expect(typedMetres("3.")).toBe(3);
});
