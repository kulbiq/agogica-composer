// A seated player in the 3D room: a chair, legs, torso and head, a music stand in front.
// Familiar sizes carry the scale (the top of the head at 1.37 m). Every player faces the
// room's centre (the conductor's spot) wherever the listener walks: chair, legs and stand are
// real 3D and turn with the view; torso and head are flat cut-outs, narrower from the side.
import type { Camera, Pt } from "./camera";
import type { Fog } from "./scene";

export function drawFigure(
  ctx: CanvasRenderingContext2D,
  cam: Camera,
  at: { x: number; y: number },
  selected: boolean,
  shows: number,
  ink: string,
  fog: Fog,
) {
  const foot = cam.toCam(at.x, at.y, 0);
  if (foot.z < 0.3) return;

  // Facing the centre; a player standing on it faces the back of the room, like the others.
  const toCentre = Math.hypot(at.x, at.y);
  const fx = toCentre > 0.05 ? -at.x / toCentre : 0;
  const fy = toCentre > 0.05 ? -at.y / toCentre : -1;
  const rx = fy;
  const ry = -fx;
  const P = (u: number, v: number, z: number): Pt => [at.x + fx * u + rx * v, at.y + fy * u + ry * v, z]; // u: forward, v: their right

  const vx = cam.x - at.x;
  const vy = cam.y - at.y;
  const seen = Math.hypot(vx, vy) || 1;
  const front = (fx * vx + fy * vy) / seen; // 1: we see their face, -1: their back
  const side = Math.sqrt(Math.max(0, 1 - front * front));
  const alpha = shows * (selected ? 0.45 : 0.28) * (1 - 0.8 * fog(seen));
  const body = `rgba(${ink},${alpha})`;
  const darker = `rgba(${ink},${alpha * 1.3})`;
  ctx.globalAlpha = 1;
  ctx.lineWidth = Math.max(1, 0.03 * cam.scaleAt(foot));

  const fill = (pts: Pt[], colour: string) => {
    ctx.beginPath();
    if (!cam.poly(ctx, pts)) return;
    ctx.fillStyle = colour;
    ctx.fill();
  };
  const chairBase = () => {
    ctx.beginPath();
    for (const [u, v] of [[-0.22, -0.22], [-0.22, 0.22], [0.2, -0.22], [0.2, 0.22]]) cam.seg(ctx, P(u, v, 0), P(u, v, 0.46));
    ctx.strokeStyle = body;
    ctx.stroke();
    fill([P(-0.23, -0.23, 0.46), P(-0.23, 0.23, 0.46), P(0.22, 0.23, 0.46), P(0.22, -0.23, 0.46)], body);
  };
  const chairBack = () => fill([P(-0.23, -0.23, 0.5), P(-0.23, 0.23, 0.5), P(-0.26, 0.23, 0.96), P(-0.26, -0.23, 0.96)], body);
  const legs = () => {
    fill([P(-0.02, -0.16, 0.5), P(-0.02, 0.16, 0.5), P(0.42, 0.16, 0.52), P(0.42, -0.16, 0.52)], body);
    fill([P(0.42, -0.16, 0), P(0.42, 0.16, 0), P(0.42, 0.16, 0.52), P(0.42, -0.16, 0.52)], body);
  };
  const torso = () => {
    const hip = cam.toCam(...P(-0.06, 0, 0.48));
    const shoulder = cam.toCam(...P(-0.06, 0, 1.05));
    const head = cam.toCam(...P(-0.04, 0, 1.27));
    if (hip.z < 0.3 || head.z < 0.3) return;
    const k = cam.scaleAt(hip);
    const [cx, yHip] = cam.project(hip);
    const [, yShoulder] = cam.project(shoulder);
    const wHip = (0.18 * Math.abs(front) + 0.15 * side) * k;
    const wShoulder = (0.21 * Math.abs(front) + 0.13 * side) * k;
    ctx.fillStyle = body;
    ctx.beginPath();
    ctx.moveTo(cx - wHip, yHip);
    ctx.lineTo(cx - wShoulder, yShoulder + 0.06 * k);
    ctx.quadraticCurveTo(cx - wShoulder, yShoulder, cx - wShoulder * 0.55, yShoulder);
    ctx.lineTo(cx + wShoulder * 0.55, yShoulder);
    ctx.quadraticCurveTo(cx + wShoulder, yShoulder, cx + wShoulder, yShoulder + 0.06 * k);
    ctx.lineTo(cx + wHip, yHip);
    ctx.closePath();
    ctx.fill();
    const [hx, hy] = cam.project(head);
    ctx.beginPath();
    ctx.arc(hx, hy, 0.1 * cam.scaleAt(head), 0, Math.PI * 2);
    ctx.fill();
  };
  const stand = () => {
    ctx.beginPath();
    cam.seg(ctx, P(0.62, 0, 0), P(0.62, 0, 0.88));
    cam.seg(ctx, P(0.62, -0.16, 0), P(0.62, 0.16, 0));
    cam.seg(ctx, P(0.5, 0, 0), P(0.74, 0, 0));
    ctx.strokeStyle = darker;
    ctx.stroke();
    // The desk leans away from the player, so the music faces them.
    fill([P(0.58, -0.25, 0.86), P(0.58, 0.25, 0.86), P(0.66, 0.25, 1.1), P(0.66, -0.25, 1.1)], darker);
  };

  chairBase();
  if (front >= 0) {
    chairBack();
    legs();
    torso();
    stand();
  } else {
    stand();
    legs();
    torso();
    chairBack();
  }
}
