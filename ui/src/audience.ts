// Where the audience sits: seats painted on a grid of squares counted from the room's centre,
// one bit each, saved with the project as base64 (C++ checks only the length and the
// alphabet: StageEdits.cpp, audienceLength). The grid covers the largest room, so a smaller
// room keeps the seats it hides. Only drawn: the audience changes nothing anyone hears.
import type { Point } from "./bridge";

export const seatPitch = 0.75; // metres: a seat and room for the knees
const reach = 32; // cells each way from the centre: 24 m, inside the largest room's 25
const side = reach * 2 + 1;
const bytes = Math.ceil((side * side) / 8);

export type Audience = Uint8Array;

export const emptyAudience = (): Audience => new Uint8Array(bytes);

// Anything that isn't a whole grid reads as no audience.
export function decodeAudience(text: string): Audience {
  const seats = emptyAudience();
  if (!text) return seats;
  try {
    const raw = atob(text);
    if (raw.length !== bytes) return seats;
    for (let i = 0; i < bytes; i++) seats[i] = raw.charCodeAt(i);
  } catch {
    // not base64
  }
  return seats;
}

// No seats at all is "": a project without an audience stores nothing.
export function encodeAudience(seats: Audience): string {
  if (seats.every((b) => b === 0)) return "";
  let raw = "";
  for (const b of seats) raw += String.fromCharCode(b);
  return btoa(raw);
}

// The grid cell nearest a point, or undefined outside the grid.
export function cellOf(p: Point): [number, number] | undefined {
  const i = Math.round(p.x / seatPitch) + 0; // + 0: never -0
  const j = Math.round(p.y / seatPitch) + 0;
  return Math.abs(i) <= reach && Math.abs(j) <= reach ? [i, j] : undefined;
}

const bit = (i: number, j: number) => (j + reach) * side + (i + reach);

export const hasSeat = (seats: Audience, i: number, j: number) => {
  const n = bit(i, j);
  return (seats[n >> 3] & (1 << (n & 7))) !== 0;
};

// Puts a seat in a cell or takes it away; cells outside the grid are left alone.
export function setSeat(seats: Audience, i: number, j: number, on: boolean) {
  if (Math.abs(i) > reach || Math.abs(j) > reach) return;
  const n = bit(i, j);
  if (on) seats[n >> 3] |= 1 << (n & 7);
  else seats[n >> 3] &= ~(1 << (n & 7));
}

// The seats inside a room of this size, clear of its walls, in metres.
export function seatsIn(seats: Audience, room: number): Point[] {
  const out: Point[] = [];
  const edge = room / 2 - 0.35;
  const last = Math.min(reach, Math.floor(edge / seatPitch));
  for (let j = -last; j <= last; j++)
    for (let i = -last; i <= last; i++) if (hasSeat(seats, i, j)) out.push({ x: i * seatPitch, y: j * seatPitch });
  return out;
}
