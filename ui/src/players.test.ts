import { expect, test } from "vitest";
import type { Player, Stage } from "./bridge";
import { seat, seatLevel } from "./players";

const track = (id: number, player = "", self = false): Player =>
  ({ id, name: `T${id}`, colour: "", icon: "", player, track: `Track ${id}`, self, x: id, y: 0 });

test("tracks with one key are one player, in the order of their first track", () => {
  const stage = { players: [track(1), track(2, "p"), track(3), track(4, "p", true), track(5, "q")] } as Stage;
  const seats = seat(stage).players;
  expect(seats.map((s) => [s.id, s.tracks.map((t) => t.id)])).toEqual([[1, [1]], [2, [2, 4]], [3, [3]], [5, [5]]]);
  expect(seats[1].self).toBe(true);
  expect(seats[1].x).toBe(2);
  expect(stage.players[1].self).toBe(false); // the snapshot itself is untouched
});

test("a player is as loud as its loudest track", () => {
  expect(seatLevel({ 1: -60, 2: -30 }, [1, 2])).toBeCloseTo(0.5);
  expect(seatLevel({}, [1])).toBe(0);
});
