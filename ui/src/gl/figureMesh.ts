// The seated players and their shadows as geometry the GPU keeps (gl/world.ts draws it with the
// camera in a shader): built again only when a player moves or the selection changes, not when
// the camera does. A seated player, part by part: chair, legs, a torso and head that turn to
// face the eyes (flat cut-outs, narrower from the side), a music stand. Familiar sizes carry the
// scale (the top of the head at 1.37 m); every player faces the room's centre (the conductor's
// spot) wherever the listener walks.
//
// Per vertex (figureStride floats):
//   x, y, z     a point of the figure, in metres (for a line: one end; for the torso and the head:
//               the point the cut-out hangs from)
//   kind        0 a face, 1 a line, 2 the torso, 3 the head
//   a, b, c     torso: across = a·|front| + b·side (metres, to the eyes' right), c up (metres);
//               head: a, b the quad's corner (±1); line: a is the side (±1)
//   ox, oy, oz  a line's other end
//   px, py      the player (for which way they face the eyes, their fog, the lines' width)
//   alpha       0.28, or 0.45 when selected; ×1.3 for the darker stand
import type { Pt } from "../room/camera";

export const figureStride = 14;

export type FigureParts = {
  // Where each part's vertices start and how many, per player, so a frame can put them in
  // the order the canvas drew them (it depends on whether we see the face or the back).
  chairBase: [number, number];
  chairBack: [number, number];
  legs: [number, number];
  torso: [number, number]; // with the head
  stand: [number, number];
};

export type FigureMesh = { data: Float32Array; players: Map<number, FigureParts>; shadows: [number, number] };

class Out {
  data: number[] = [];
  get vertices() {
    return this.data.length / figureStride;
  }
}

export function buildFigures(players: readonly { id: number; x: number; y: number; selected: boolean }[]): FigureMesh {
  const out = new Out();
  const parts = new Map<number, FigureParts>();

  for (const pl of players) {
    const toCentre = Math.hypot(pl.x, pl.y);
    const fx = toCentre > 0.05 ? -pl.x / toCentre : 0;
    const fy = toCentre > 0.05 ? -pl.y / toCentre : -1;
    const rx = fy;
    const ry = -fx;
    const P = (u: number, v: number, z: number): Pt => [pl.x + fx * u + rx * v, pl.y + fy * u + ry * v, z]; // u: forward, v: their right
    const alpha = pl.selected ? 0.45 : 0.28;

    const vertex = (p: Pt, kind: number, a: number, b: number, c: number, o: Pt, k: number) =>
      out.data.push(p[0], p[1], p[2], kind, a, b, c, o[0], o[1], o[2], pl.x, pl.y, alpha * k, 0);
    const face = (pts: Pt[], k = 1) => {
      for (let i = 1; i + 1 < pts.length; i++)
        for (const p of [pts[0], pts[i], pts[i + 1]]) vertex(p, 0, 0, 0, 0, p, k);
    };
    // The shader widens a line across the direction from this end to the other, so at the
    // far end that direction is reversed and the side is too.
    const line = (a: Pt, b: Pt, k = 1) => {
      vertex(a, 1, 1, 0, 0, b, k); vertex(b, 1, -1, 0, 0, a, k); vertex(b, 1, 1, 0, 0, a, k);
      vertex(a, 1, 1, 0, 0, b, k); vertex(b, 1, 1, 0, 0, a, k); vertex(a, 1, -1, 0, 0, b, k);
    };
    const part = (draw: () => void): [number, number] => {
      const first = out.vertices;
      draw();
      return [first, out.vertices - first];
    };

    const chairBase = part(() => {
      for (const [u, v] of [[-0.22, -0.22], [-0.22, 0.22], [0.2, -0.22], [0.2, 0.22]]) line(P(u, v, 0), P(u, v, 0.46));
      face([P(-0.23, -0.23, 0.46), P(-0.23, 0.23, 0.46), P(0.22, 0.23, 0.46), P(0.22, -0.23, 0.46)]);
    });
    const chairBack = part(() => face([P(-0.23, -0.23, 0.5), P(-0.23, 0.23, 0.5), P(-0.26, 0.23, 0.96), P(-0.26, -0.23, 0.96)]));
    const legs = part(() => {
      face([P(-0.02, -0.16, 0.5), P(-0.02, 0.16, 0.5), P(0.42, 0.16, 0.52), P(0.42, -0.16, 0.52)]);
      face([P(0.42, -0.16, 0), P(0.42, 0.16, 0), P(0.42, 0.16, 0.52), P(0.42, -0.16, 0.52)]);
    });
    const torso = part(() => {
      // A cut-out from the hips to the shoulders, the shoulders rounded: its outline as
      // (anchor, fraction of the row's width, metres up), fanned from the first point.
      const hip = P(-0.06, 0, 0.48);
      const shoulder = P(-0.06, 0, 1.05);
      type Row = "hip" | "shoulder";
      const outline: [Row, number, number][] = [["hip", -1, 0], ["shoulder", -1, -0.06]];
      const curve = (f0: number, z0: number, fq: number, zq: number, f1: number, z1: number) => {
        for (let i = 1; i <= 8; i++) {
          const s = i / 8;
          outline.push(["shoulder", (1 - s) * (1 - s) * f0 + 2 * (1 - s) * s * fq + s * s * f1, (1 - s) * (1 - s) * z0 + 2 * (1 - s) * s * zq + s * s * z1]);
        }
      };
      curve(-1, -0.06, -1, 0, -0.55, 0);
      outline.push(["shoulder", 0.55, 0]);
      curve(0.55, 0, 1, 0, 1, -0.06);
      outline.push(["hip", 1, 0]);
      const corner = ([row, f, z]: [Row, number, number]) =>
        row === "hip" ? vertex(hip, 2, f * 0.18, f * 0.15, z, hip, 1) : vertex(shoulder, 2, f * 0.21, f * 0.13, z, shoulder, 1);
      for (let i = 1; i + 1 < outline.length; i++) {
        corner(outline[0]);
        corner(outline[i]);
        corner(outline[i + 1]);
      }
      // The head: a disc 0.1 m across, cut out in the shader.
      const head = P(-0.04, 0, 1.27);
      for (const [a, b] of [[-1, -1], [1, -1], [1, 1], [-1, -1], [1, 1], [-1, 1]]) vertex(head, 3, a, b, 0, head, 1);
    });
    const stand = part(() => {
      line(P(0.62, 0, 0), P(0.62, 0, 0.88), 1.3);
      line(P(0.62, -0.16, 0), P(0.62, 0.16, 0), 1.3);
      line(P(0.5, 0, 0), P(0.74, 0, 0), 1.3);
      // The desk leans away from the player, so the music faces them.
      face([P(0.58, -0.25, 0.86), P(0.58, 0.25, 0.86), P(0.66, 0.25, 1.1), P(0.66, -0.25, 1.1)], 1.3);
    });
    parts.set(pl.id, { chairBase, chairBack, legs, torso, stand });
  }

  // Shadows on the floor, a 16-sided disc 0.38 m across under each player (alpha from the look).
  const shadows = part(out, () => {
    for (const pl of players) {
      const at = (i: number): Pt => [pl.x + Math.cos((i / 16) * Math.PI * 2) * 0.38, pl.y + Math.sin((i / 16) * Math.PI * 2) * 0.38, 0];
      for (let i = 1; i + 1 < 16; i++)
        for (const p of [at(0), at(i), at(i + 1)]) out.data.push(p[0], p[1], p[2], 0, 0, 0, 0, p[0], p[1], p[2], pl.x, pl.y, 1, 0);
    }
  });

  return { data: new Float32Array(out.data), players: parts, shadows };
}

function part(out: Out, draw: () => void): [number, number] {
  const first = out.vertices;
  draw();
  return [first, out.vertices - first];
}

// The order the canvas drew a figure's parts: seen from the front the chair back is behind the
// body and the stand in front of it; from behind, the other way round.
export function partOrder(parts: FigureParts, front: boolean): [number, number][] {
  return front
    ? [parts.chairBase, parts.chairBack, parts.legs, parts.torso, parts.stand]
    : [parts.chairBase, parts.stand, parts.legs, parts.torso, parts.chairBack];
}
