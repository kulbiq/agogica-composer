import type { Player, Stage } from "./bridge";
import { levelOf, type Levels } from "./level";

// A player as the UI shows it: one track, or several sharing a player key (one place, name
// and icon - C++ keeps them equal on every track). It takes its first track's id, place, name
// and icon; `self` if this window's track is one of its tracks.
export type Seat = Player & { tracks: Player[] };
export type SeatedStage = Omit<Stage, "players"> & { players: Seat[] };

export function seat(stage: Stage): SeatedStage {
  const seats: Seat[] = [];
  const byKey = new Map<string, Seat>();

  for (const p of stage.players) {
    const found = p.player ? byKey.get(p.player) : undefined;
    if (found) {
      found.tracks.push(p);
      found.self ||= p.self;
      continue;
    }
    const s: Seat = { ...p, tracks: [p] };
    seats.push(s);
    if (p.player) byKey.set(p.player, s);
  }

  return { ...stage, players: seats };
}

// A player is as loud as its loudest track.
export const seatLevel = (levels: Levels, ids: number[]) => Math.max(0, ...ids.map((id) => levelOf(levels, id)));
