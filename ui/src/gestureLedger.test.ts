import { expect, test } from "vitest";
import { GestureLedger } from "./gestureLedger";

test("begin, changes, end is a gesture; anything else throws", () => {
  const ledger = new GestureLedger();
  ledger.record("begin", 1, "playerX");
  ledger.record("change", 1, "playerX");
  ledger.record("end", 1, "playerX");
  expect(ledger.openGestures).toEqual([]);

  expect(() => ledger.record("change", 1, "playerX")).toThrow("outside a gesture");
  expect(() => ledger.record("end", 1, "playerX")).toThrow("outside a gesture");

  ledger.record("begin", 1, "playerX");
  expect(() => ledger.record("begin", 1, "playerX")).toThrow("began twice");
  ledger.record("begin", 2, "playerX"); // another track's parameter is another gesture
  expect(ledger.openGestures).toEqual(["1/playerX", "2/playerX"]);
});
