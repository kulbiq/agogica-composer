// The room around the stage in the 3D view: floorboards, panelled walls with doors and windows,
// a beamed ceiling, audience seats. Sizes everyone knows (a 2.1 m door, a 1.2 m window over a
// radiator, a seat) carry the scale. The layout depends only on the room size (the seats are
// where the user painted them: audience.ts); materials come from styles/room.css. The GPU draws
// them from the meshes in gl/roomMesh.ts.
import type { Pt } from "./camera";

export type RGB = readonly [number, number, number];

export type Materials = {
  wood: RGB[];
  seam: RGB;
  wall: RGB[];
  wainscot: RGB;
  skirting: RGB;
  ceiling: RGB;
  beam: RGB;
  door: RGB;
  glass: RGB;
  seat: RGB;
  seatBack: RGB;
  fog: RGB;
  shade: string; // "r,g,b"
  shadow: number; // alpha
  vignette: number;
};

export function readMaterials(style: CSSStyleDeclaration): Materials {
  const rgb = (name: string): RGB => {
    const [r, g, b] = style.getPropertyValue(`--room-${name}`).split(",").map(Number);
    return [r || 0, g || 0, b || 0];
  };
  return {
    wood: [1, 2, 3, 4, 5].map((i) => rgb(`wood-${i}`)),
    seam: rgb("seam"),
    wall: [rgb("wall-1"), rgb("wall-2")],
    wainscot: rgb("wainscot"),
    skirting: rgb("skirting"),
    ceiling: rgb("ceiling"),
    beam: rgb("beam"),
    door: rgb("door"),
    glass: rgb("glass"),
    seat: rgb("seat"),
    seatBack: rgb("seat-back"),
    fog: rgb("fog"),
    shade: style.getPropertyValue("--room-shade-rgb").trim() || "0,0,0",
    shadow: Number(style.getPropertyValue("--room-shadow")) || 0.2,
    vignette: Number(style.getPropertyValue("--room-vignette")) || 0.16,
  };
}

// ---- Layout ----

type Plank = { x0: number; x1: number; y0: number; y1: number; tone: number };
// A wall, by which side of the room it closes: u runs along it, left to right as seen from inside.
export type Wall = "front" | "back" | "right" | "left";
type Opening = { wall: Wall; at: number }; // `at`: where along the wall it starts, metres
export type Layout = { room: number; planks: Plank[]; doors: Opening[]; windows: Opening[] };

const doorWidth = 0.9;
const windowWidth = 1.2;

export function layoutRoom(room: number): Layout {
  const half = room / 2;

  // Boards 18 cm wide in random lengths, the same pattern every time.
  const planks: Plank[] = [];
  let seed = 11;
  const random = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
  for (let x = -half; x < half - 1e-6; x += 0.18) {
    let y = -half - random() * 2;
    while (y < half) {
      const length = 1.2 + random() * 1.6;
      const y0 = Math.max(-half, y);
      const y1 = Math.min(half, y + length);
      if (y1 > y0) planks.push({ x0: x, x1: Math.min(half, x + 0.18), y0, y1, tone: Math.floor(random() * 5) });
      y += length;
    }
  }

  // Doors at the back of the side walls and on the front wall; windows along the side walls
  // wherever there is no door.
  const doors = ([
    { wall: "left", at: -0.6 * half },
    { wall: "right", at: 0.2 * half },
    { wall: "front", at: 0.6 * half },
  ] satisfies Opening[]).filter((d) => d.at + doorWidth <= half - 0.3);
  const windows: Opening[] = [];
  for (const wall of ["left", "right"] as const)
    for (let at = -half + 1.4; at + windowWidth <= half - 1; at += 4) {
      const clear = doors.every((d) => d.wall !== wall || at + windowWidth + 0.4 < d.at || at > d.at + doorWidth + 0.4);
      if (clear) windows.push({ wall, at });
    }

  return { room, planks, doors, windows };
}

// A point on a wall's inner face: `u` along it, `z` up, `inset` into the room.
export function onWall(wall: Wall, half: number, u: number, z: number, inset = 0): Pt {
  switch (wall) {
    case "front": return [u, half - inset, z];
    case "back": return [-u, -half + inset, z];
    case "right": return [half - inset, -u, z];
    case "left": return [-half + inset, u, z];
  }
}

// Dashed arcs on the floor ahead of the listener: where 3, 5 and 10 m really are.
export const distanceArcs = [3, 5, 10];
