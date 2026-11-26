import { expect, test } from "vitest";
import { easeToward, wrapDegrees } from "./ease";

test("an ease covers the same distance however the time is cut up", () => {
  expect(easeToward(0, 10, 0.25, 1 / 60)).toBeCloseTo(2.5, 12); // a 60 Hz frame: a quarter of the way
  const twoHalves = easeToward(easeToward(0, 10, 0.25, 1 / 120), 10, 0.25, 1 / 120);
  expect(twoHalves).toBeCloseTo(2.5, 12);
  const oneLong = easeToward(0, 10, 0.25, 1 / 30);
  expect(oneLong).toBeCloseTo(easeToward(2.5, 10, 0.25, 1 / 60), 12);
  expect(easeToward(3, 10, 0.25, 0)).toBe(3);
});

test("turning wraps around the back, within listenerYaw's range", () => {
  expect(wrapDegrees(175 + 15)).toBe(-170);
  expect(wrapDegrees(-175 - 15)).toBe(170);
  expect(wrapDegrees(180)).toBe(180);
  expect(wrapDegrees(-180)).toBe(-180);
  expect(wrapDegrees(12.34)).toBe(12.3);
});
