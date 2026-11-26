// Draws the stage with WebGL: the room and the audience seats as meshes that stay on the GPU
// (built again only when the room, its look or the seats change), and the screen-space
// triangles of the map and the players (gl/tris.ts) built for each frame. The camera of
// room/camera.ts runs in the shaders, Panini lens included, so a frame costs a few draw calls
// however many boards and seats there are.
//
// WebGL 1 (every WebView has it) with OES_standard_derivatives for the floorboards' seams.
import type { Shot } from "../room/camera";
import type { RGB } from "../room/scene";
import { figureStride } from "./figureMesh";
import { overlayStride, SpriteAtlas, type OverlayQuads } from "./sprites";
import { faceStride, lineStride, type RoomMesh } from "./roomMesh";
import { trisStride, type Group, type Tris } from "./tris";

// Depth range, metres: the near plane is room/camera.ts's; the far one holds the map's camera
// (60 m up) looking at the largest room.
const near = 0.05;
const far = 400;

const common = `
precision highp float;
uniform vec3 uCam;   // camera position
uniform vec4 uRot;   // cos yaw, sin yaw, cos pitch, sin pitch
uniform vec3 uLens;  // focal (px), Panini d, squeeze
uniform vec4 uView;  // straight ahead on screen (cx, cy), size (W, H), CSS px
const float NEAR = ${near.toFixed(3)};
const float FAR = ${far.toFixed(1)};

vec3 toCam(vec3 p) {
  vec3 d = p - uCam;
  float right = d.x * uRot.x - d.y * uRot.y;
  float fwd = d.x * uRot.y + d.y * uRot.x;
  return vec3(right, -fwd * uRot.w + d.z * uRot.z, fwd * uRot.z + d.z * uRot.w);
}

// Camera.project: the general Panini projection, to CSS px. Points ahead only.
vec2 screenOf(vec3 c) {
  float r = length(c.xz);
  float cosA = c.z / r;
  float d = uLens.y;
  float s = (d + 1.0) / (d + cosA);
  float t = c.y / r;
  return vec2(uView.x + uLens.x * s * c.x / r, uView.y - uLens.x * s * (t + (t / cosA - t) * uLens.z));
}

float depthNdc(float z) {
  return ((FAR + NEAR) - 2.0 * FAR * NEAR / z) / (FAR - NEAR);
}
`;

// Faces: projected per vertex, and handed to the GPU as perspective (w = depth) scaled by how
// far Panini moves the point from plain perspective, so triangles that cross the near plane
// are clipped by the GPU.
const facesVertex = `${common}
attribute vec3 aPos;
attribute vec3 aColour;
attribute vec3 aUvKind;
varying vec3 vWorld;
varying vec3 vColour;
varying vec3 vUvKind;
void main() {
  vec3 c = toCam(aPos);
  float r = max(length(c.xz), 1e-6);
  float cosA = max(c.z / r, 0.0);
  float d = uLens.y;
  float k = d < 1e-6 ? 1.0 : (d + 1.0) * cosA / (d + cosA);
  float q = 1.0 + (1.0 / max(cosA, 1e-3) - 1.0) * uLens.z;
  float x = uView.x * c.z + uLens.x * k * c.x;
  float y = uView.y * c.z - uLens.x * k * c.y * q;
  gl_Position = vec4(2.0 * x / uView.z - c.z, c.z - 2.0 * y / uView.w, ((FAR + NEAR) * c.z - 2.0 * FAR * NEAR) / (FAR - NEAR), c.z);
  vWorld = aPos;
  vColour = aColour;
  vUvKind = aUvKind;
}`;

const facesFragment = `#extension GL_OES_standard_derivatives : enable
${common}
uniform vec3 uFog;
uniform float uShows;  // how much of the room shows: the fog grows with it
uniform float uAlpha;
uniform float uDpr;
uniform vec3 uSeam;
uniform vec3 uAverage;
varying vec3 vWorld;
varying vec3 vColour;
varying vec3 vUvKind;
void main() {
  vec3 c = vColour;
  float dist = distance(vWorld, uCam);
  if (vUvKind.z > 0.5) {
    // A floorboard: its average colour once boards are only a few px wide (no shimmer), and
    // its seams (a 0.6 px line, as the canvas drew them) where they can be seen, within 16 m.
    vec2 w = max(fwidth(vUvKind.xy), vec2(1e-6));
    float across = 1.0 / w.x; // the board's width in device px
    c = mix(uAverage, c, smoothstep(2.0, 5.0, across));
    vec2 edge = min(vUvKind.xy, 1.0 - vUvKind.xy) / w; // device px to its nearest edges
    float cover = clamp(0.3 * uDpr + 0.5 - min(edge.x, edge.y), 0.0, 1.0);
    float seam = cover * smoothstep(3.0, 6.0, across) * (1.0 - smoothstep(14.0, 16.0, dist));
    c = mix(c, uSeam, seam);
  }
  c = mix(c, uFog, clamp((dist - 3.0) / 24.0, 0.0, 0.72) * uShows);
  gl_FragColor = vec4(c * uAlpha, uAlpha);
}`;

// Lines a constant width on screen: both ends are projected and the quad is widened across.
const linesVertex = `${common}
attribute vec3 aA;
attribute vec3 aB;
attribute vec3 aSideEndWidth;
attribute vec4 aColour;
varying vec3 vWorld;
varying vec4 vColour;
void main() {
  vec3 ca = toCam(aA);
  vec3 cb = toCam(aB);
  if (ca.z < NEAR && cb.z < NEAR) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
  }
  if (ca.z < NEAR) ca = mix(ca, cb, (NEAR - ca.z) / (cb.z - ca.z));
  if (cb.z < NEAR) cb = mix(cb, ca, (NEAR - cb.z) / (ca.z - cb.z));
  bool atB = aSideEndWidth.y > 0.5;
  vec2 sa = screenOf(ca);
  vec2 sb = screenOf(cb);
  vec2 dir = sb - sa;
  float len = length(dir);
  vec2 n = len > 1e-6 ? vec2(-dir.y, dir.x) / len : vec2(0.0);
  vec2 s = (atB ? sb : sa) + n * aSideEndWidth.x * aSideEndWidth.z * 0.5;
  gl_Position = vec4(s.x / uView.z * 2.0 - 1.0, 1.0 - s.y / uView.w * 2.0, depthNdc(atB ? cb.z : ca.z), 1.0);
  vWorld = atB ? aB : aA;
  vColour = aColour;
}`;

const linesFragment = `${common}
uniform vec3 uFog;
uniform float uShows;
varying vec3 vWorld;
varying vec4 vColour;
void main() {
  float dist = distance(vWorld, uCam);
  vec3 c = mix(vColour.rgb, uFog, clamp((dist - 3.0) / 24.0, 0.0, 0.72) * uShows);
  float a = vColour.a * uShows;
  gl_FragColor = vec4(c * a, a);
}`;

// Screen-space triangles (gl/tris.ts): CSS px, with the depth of what they belong to.
const trisVertex = `${common}
attribute vec2 aPos;
attribute vec4 aColour;
attribute vec4 aCircle; // centre, outer and inner radius (CSS px); outer 0: a plain triangle
varying vec4 vColour;
varying vec2 vAt;
varying vec4 vCircle;
void main() {
  gl_Position = vec4(aPos.x / uView.z * 2.0 - 1.0, 1.0 - aPos.y / uView.w * 2.0, 0.0, 1.0);
  vColour = aColour;
  vAt = aPos;
  vCircle = aCircle;
}`;

// A circle's coverage is worked out per pixel: exact and smooth at any size, from one quad.
const trisFragment = `precision highp float;
uniform float uDpr;
varying vec4 vColour;
varying vec2 vAt;
varying vec4 vCircle;
void main() {
  float a = vColour.a;
  if (vCircle.z > 0.0) {
    float d = distance(vAt, vCircle.xy);
    a *= clamp((vCircle.z - d) * uDpr + 0.5, 0.0, 1.0);
    if (vCircle.w > 0.0) a *= clamp((d - vCircle.w) * uDpr + 0.5, 0.0, 1.0);
    if (a <= 0.0) discard;
  }
  gl_FragColor = vec4(vColour.rgb * a, a);
}`;

// The players (gl/figureMesh.ts): faces, lines, the torso that turns to the eyes, the head.
const figuresVertex = `${common}
attribute vec4 aPosKind;
attribute vec3 aABC;
attribute vec3 aOther;
attribute vec3 aPlayerAlpha;
uniform float uShows;
uniform float uFogged;     // 1 for the figures (fainter in the fog), 0 for the shadows
uniform float uAlphaScale;
varying float vAlpha;
varying vec3 vDisc;        // the head: centre and radius on screen, CSS px (radius 0: not a disc)

float scaleAt(vec3 c) {
  float r = length(c.xz);
  float d = uLens.y;
  return uLens.x * (d + 1.0) / (d + c.z / r) / r;
}

vec4 clipOf(vec3 c) {
  float r = max(length(c.xz), 1e-6);
  float cosA = max(c.z / r, 0.0);
  float d = uLens.y;
  float k = d < 1e-6 ? 1.0 : (d + 1.0) * cosA / (d + cosA);
  float q = 1.0 + (1.0 / max(cosA, 1e-3) - 1.0) * uLens.z;
  float x = uView.x * c.z + uLens.x * k * c.x;
  float y = uView.y * c.z - uLens.x * k * c.y * q;
  return vec4(2.0 * x / uView.z - c.z, c.z - 2.0 * y / uView.w, ((FAR + NEAR) * c.z - 2.0 * FAR * NEAR) / (FAR - NEAR), c.z);
}

vec4 screenClip(vec2 s, float z) {
  return vec4(s.x / uView.z * 2.0 - 1.0, 1.0 - s.y / uView.w * 2.0, depthNdc(z), 1.0);
}

void main() {
  vec3 p = aPosKind.xyz;
  float kind = aPosKind.w;
  vec2 player = aPlayerAlpha.xy;
  vec2 toEyes = uCam.xy - player;
  float seen = max(length(toEyes), 1e-6);
  vec2 facing = length(player) > 0.05 ? -normalize(player) : vec2(0.0, -1.0);
  float front = dot(facing, toEyes / seen); // 1: we see their face, -1: their back
  float side = sqrt(max(0.0, 1.0 - front * front));
  float fog = clamp((seen - 3.0) / 24.0, 0.0, 0.72) * uShows;
  vAlpha = aPlayerAlpha.z * uAlphaScale * mix(1.0, 1.0 - 0.8 * fog, uFogged);
  vDisc = vec3(0.0);
  vec3 right = vec3(uRot.x, -uRot.y, 0.0); // the eyes' right, in the world

  if (kind < 0.5) {
    gl_Position = clipOf(toCam(p));
  } else if (kind < 1.5) {
    // A line, as wide as the canvas drew it: 3 cm at the player's feet, at least 1 px.
    vec3 ca = toCam(p);
    vec3 cb = toCam(aOther);
    if (ca.z < NEAR && cb.z < NEAR) {
      gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
      return;
    }
    if (ca.z < NEAR) ca = mix(ca, cb, (NEAR - ca.z) / (cb.z - ca.z));
    if (cb.z < NEAR) cb = mix(cb, ca, (NEAR - cb.z) / (ca.z - cb.z));
    vec2 sa = screenOf(ca);
    vec2 sb = screenOf(cb);
    vec2 dir = sb - sa;
    float len = length(dir);
    vec2 n = len > 1e-6 ? vec2(-dir.y, dir.x) / len : vec2(0.0);
    float width = max(1.0, 0.03 * scaleAt(toCam(vec3(player, 0.0))));
    gl_Position = screenClip(sa + n * aABC.x * width * 0.5, ca.z);
  } else if (kind < 2.5) {
    // The torso: a cut-out facing the eyes, narrower seen from the side.
    gl_Position = clipOf(toCam(p + right * (aABC.x * abs(front) + aABC.y * side) + vec3(0.0, 0.0, aABC.z)));
  } else {
    // The head: a disc on screen, cut out per pixel in the fragment shader.
    vec3 c = toCam(p);
    float radius = 0.1 * scaleAt(c);
    vec2 centre = screenOf(c);
    vDisc = vec3(centre, radius);
    gl_Position = screenClip(centre + aABC.xy * (radius + 1.0), c.z);
  }
}`;

const figuresFragment = `precision highp float;
uniform vec4 uView;
uniform float uDpr;
uniform vec3 uColour;
varying float vAlpha;
varying vec3 vDisc;
void main() {
  float a = vAlpha;
  if (vDisc.z > 0.0) {
    vec2 at = vec2(gl_FragCoord.x, uView.w * uDpr - gl_FragCoord.y) / uDpr;
    a *= clamp((vDisc.z - distance(at, vDisc.xy)) * uDpr + 0.5, 0.0, 1.0);
    if (a <= 0.0) discard;
  }
  gl_FragColor = vec4(uColour * a, a);
}`;

// The overlay (gl/sprites.ts): atlas sprites copied one texel per pixel (premultiplied),
// icons from the mipmapped sheet in a colour, and solid triangles and circles.
const overlayVertex = `${common}
attribute vec4 aPosUv;
attribute float aMode;
attribute vec4 aColour;
attribute vec4 aCircle;
varying vec2 vUv;
varying float vMode;
varying vec4 vColour;
varying vec2 vAt;
varying vec4 vCircle;
void main() {
  gl_Position = vec4(aPosUv.x / uView.z * 2.0 - 1.0, 1.0 - aPosUv.y / uView.w * 2.0, 0.0, 1.0);
  vUv = aPosUv.zw;
  vMode = aMode;
  vColour = aColour;
  vAt = aPosUv.xy;
  vCircle = aCircle;
}`;

const overlayFragment = `precision highp float;
uniform sampler2D uAtlas;
uniform sampler2D uIcons;
uniform float uDpr;
varying vec2 vUv;
varying float vMode;
varying vec4 vColour;
varying vec2 vAt;
varying vec4 vCircle;
void main() {
  if (vMode < 0.5) {
    gl_FragColor = texture2D(uAtlas, vUv) * vColour.a;
  } else if (vMode < 1.5) {
    float a = texture2D(uIcons, vUv).a * vColour.a;
    gl_FragColor = vec4(vColour.rgb * a, a);
  } else {
    float a = vColour.a;
    if (vCircle.z > 0.0) {
      float d = distance(vAt, vCircle.xy);
      a *= clamp((vCircle.z - d) * uDpr + 0.5, 0.0, 1.0);
      if (vCircle.w > 0.0) a *= clamp((d - vCircle.w) * uDpr + 0.5, 0.0, 1.0);
      if (a <= 0.0) discard;
    }
    gl_FragColor = vec4(vColour.rgb * a, a);
  }
}`;

// The kept picture of what stands still (see render): copied back one texel per pixel.
const blitVertex = `attribute vec2 aPos;
varying vec2 vUv;
void main() {
  gl_Position = vec4(aPos, 0.0, 1.0);
  vUv = aPos * 0.5 + 0.5;
}`;

const blitFragment = `precision mediump float;
uniform sampler2D uStill;
varying vec2 vUv;
void main() {
  gl_FragColor = texture2D(uStill, vUv);
}`;

// The vignette: a soft dark rim, as the canvas's radial gradient drew it.
const vignetteVertex = `attribute vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }`;

const vignetteFragment = `precision highp float;
uniform vec4 uView;    // unused cx, cy; W, H in CSS px
uniform float uDpr;
uniform vec4 uShade;   // rgb, alpha at the rim (already times how much the room shows)
void main() {
  vec2 p = vec2(gl_FragCoord.x, uView.w * uDpr - gl_FragCoord.y) / uDpr;
  vec2 centre = vec2(uView.z / 2.0, uView.w * 0.55);
  float r0 = min(uView.z, uView.w) * 0.3;
  float r1 = max(uView.z, uView.w) * 0.75;
  float a = clamp((distance(p, centre) - r0) / (r1 - r0), 0.0, 1.0) * uShade.a;
  gl_FragColor = vec4(uShade.rgb * a, a);
}`;

type Program = { program: WebGLProgram; attributes: Record<string, number>; uniforms: Record<string, WebGLUniformLocation | null> };

export type WorldFrame = {
  shot: Shot;
  width: number; // CSS px
  height: number;
  dpr: number;
  shows: number; // how much of the room shows, 0 on the map
  ceilingShows: number;
  fog: RGB;
  seam: RGB;
  average: RGB;
  vignette: { shade: RGB; alpha: number }; // alpha already times `shows`
  before: Tris; // the map's lines and the distance arcs (under the vignette)
  after: Tris; // the azimuth line and the audience on the map (over the vignette and the shadows, under the seats)
  ink: RGB;
  shadows?: { first: number; count: number; shade: RGB; alpha: number }; // under the players, in the room
  figures?: Uint16Array | Uint32Array; // the players' parts, far to near (indices into setFigures' mesh)
  stillKey: string; // everything but the halos, rings and overlay: equal keys draw equal pictures
  players: Tris; // halos and rings: over everything
  overlay: OverlayQuads; // badges, names, labels, the arrow, the minimap: on top, in order
};

export class WorldRenderer {
  private readonly gl: WebGLRenderingContext;
  private faces: Program;
  private lines: Program;
  private tris: Program;
  private vignette: Program;
  private roomFaces: WebGLBuffer;
  private roomCeiling: WebGLBuffer;
  private roomLines: WebGLBuffer;
  private seats: WebGLBuffer;
  private trisBuffer: WebGLBuffer;
  private quad: WebGLBuffer;
  private figures: Program;
  private overlayProgram: Program;
  private blit: Program;
  private still: { texture: WebGLTexture; key: string; width: number; height: number };
  private lastStillKey = "";
  readonly atlas: SpriteAtlas;
  private figureVertices: WebGLBuffer;
  private figureOrder: WebGLBuffer;
  private wideIndices: boolean; // OES_element_index_uint: more than 65536 figure vertices
  private counts = { faces: 0, ceiling: 0, lines: 0, seats: 0, figures: 0 };

  constructor(private readonly canvas: HTMLCanvasElement) {
    const gl = canvas.getContext("webgl", { alpha: true, antialias: true, depth: true, stencil: true, premultipliedAlpha: true, preserveDrawingBuffer: false });
    if (!gl) throw new Error("WebGL is not available");
    if (!gl.getExtension("OES_standard_derivatives")) throw new Error("WebGL lacks OES_standard_derivatives");
    this.gl = gl;
    this.faces = program(gl, facesVertex, facesFragment);
    this.lines = program(gl, linesVertex, linesFragment);
    this.tris = program(gl, trisVertex, trisFragment);
    this.vignette = program(gl, vignetteVertex, vignetteFragment);
    this.figures = program(gl, figuresVertex, figuresFragment);
    this.overlayProgram = program(gl, overlayVertex, overlayFragment);
    this.blit = program(gl, blitVertex, blitFragment);
    const still = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D, still);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    this.still = { texture: still, key: "", width: 0, height: 0 };
    this.atlas = new SpriteAtlas(gl, canvas);
    this.wideIndices = !!gl.getExtension("OES_element_index_uint");
    const buffer = () => gl.createBuffer()!;
    this.figureVertices = buffer();
    this.figureOrder = buffer();
    this.roomFaces = buffer();
    this.roomCeiling = buffer();
    this.roomLines = buffer();
    this.seats = buffer();
    this.trisBuffer = buffer();
    this.quad = buffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1]), gl.STATIC_DRAW);
  }

  setRoom(mesh: RoomMesh) {
    const { gl } = this;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.roomFaces);
    gl.bufferData(gl.ARRAY_BUFFER, mesh.faces, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.roomCeiling);
    gl.bufferData(gl.ARRAY_BUFFER, mesh.ceiling, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.roomLines);
    gl.bufferData(gl.ARRAY_BUFFER, mesh.lines, gl.STATIC_DRAW);
    this.counts.faces = mesh.faces.length / faceStride;
    this.counts.ceiling = mesh.ceiling.length / faceStride;
    this.counts.lines = mesh.lines.length / lineStride;
  }

  setSeats(seats: Float32Array) {
    const { gl } = this;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.seats);
    gl.bufferData(gl.ARRAY_BUFFER, seats, gl.STATIC_DRAW);
    this.counts.seats = seats.length / faceStride;
  }

  // Everything this made, given back: the page's part (the atlas's scratch canvas) and the
  // GPU's. The context itself lives as long as its canvas, and a canvas has only ever one, so
  // without this every map that was ever shown would keep its buffers and its atlas (16 MB).
  dispose() {
    const { gl } = this;
    this.atlas.dispose();
    for (const p of [this.faces, this.lines, this.tris, this.vignette, this.figures, this.overlayProgram, this.blit]) gl.deleteProgram(p.program);
    for (const b of [this.figureVertices, this.figureOrder, this.roomFaces, this.roomCeiling, this.roomLines, this.seats, this.trisBuffer, this.quad])
      gl.deleteBuffer(b);
    gl.deleteTexture(this.still.texture);
  }

  setFigures(data: Float32Array) {
    const { gl } = this;
    gl.bindBuffer(gl.ARRAY_BUFFER, this.figureVertices);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
    this.counts.figures = data.length / figureStride;
  }

  // Indices into the figure mesh in the order they are drawn (16 bits while they fit).
  indexArray(n: number) {
    return this.counts.figures > 65535 && this.wideIndices ? new Uint32Array(n) : new Uint16Array(n);
  }

  render(f: WorldFrame) {
    const { gl, canvas } = this;
    const pw = Math.max(1, Math.round(f.width * f.dpr));
    const ph = Math.max(1, Math.round(f.height * f.dpr));
    if (canvas.width !== pw) canvas.width = pw;
    if (canvas.height !== ph) canvas.height = ph;
    gl.viewport(0, 0, pw, ph);
    gl.clearColor(0, 0, 0, 0);
    gl.clearDepth(1);
    gl.clearStencil(0);
    gl.disable(gl.STENCIL_TEST);
    gl.depthMask(true);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT | gl.STENCIL_BUFFER_BIT);
    gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA); // premultiplied: the canvas's source-over
    gl.depthFunc(gl.LEQUAL);

    // Everything that stands still while the camera does (the room, seats, figures, the map's
    // lines) is kept as a picture once a frame shows it unchanged: a frame where only the levels
    // move (the halos) copies that picture and draws the halos and the overlay over it.
    const cache = this.still;
    if (cache.key === f.stillKey && cache.width === pw && cache.height === ph) this.drawStill();
    else {
      this.drawStillParts(f);
      if (f.stillKey === this.lastStillKey) {
        gl.bindTexture(gl.TEXTURE_2D, cache.texture);
        if (cache.width !== pw || cache.height !== ph) {
          gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, pw, ph, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
          cache.width = pw;
          cache.height = ph;
        }
        gl.copyTexSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 0, 0, pw, ph);
        cache.key = f.stillKey;
      } else cache.key = "";
    }
    this.lastStillKey = f.stillKey;

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.STENCIL_TEST);
    gl.enable(gl.BLEND);
    gl.depthMask(false);
    this.drawTris(f, f.players);
    this.drawOverlay(f, f.overlay);
  }

  private drawStillParts(f: WorldFrame) {
    const { gl } = this;
    const room = f.shows > 0.002;
    if (room) {
      // Opaque surfaces: the nearest wins, as when the canvas painted far to near.
      gl.enable(gl.DEPTH_TEST);
      gl.depthMask(true);
      gl.disable(gl.BLEND);
      this.drawFaces(f, this.roomFaces, this.counts.faces, f.shows);
      if (f.ceilingShows > 0.002) this.drawFaces(f, this.roomCeiling, this.counts.ceiling, f.ceilingShows);
      gl.enable(gl.BLEND);
      gl.depthMask(false);
      this.drawLines(f);
    }

    gl.disable(gl.DEPTH_TEST);
    gl.enable(gl.BLEND);
    gl.depthMask(false);
    this.stencilBase = 0;
    this.drawTris(f, f.before);
    if (room && f.vignette.alpha > 0.001) this.drawVignette(f);
    if (room && f.shadows) this.drawFigureMesh(f, f.shadows.shade, f.shadows.alpha, 0, { first: f.shadows.first, count: f.shadows.count });
    this.drawTris(f, f.after);

    // The seats: over the floor they stand on (nothing of the room is ever in front of them),
    // so the depth buffer starts again and holds only them. The figures are tested against it.
    gl.depthMask(true);
    gl.clear(gl.DEPTH_BUFFER_BIT);
    if (room && this.counts.seats > 0) {
      gl.enable(gl.DEPTH_TEST);
      gl.disable(gl.BLEND);
      this.drawFaces(f, this.seats, this.counts.seats, f.shows);
    }

    // Figures, far to near: hidden behind nearer seats, blended over everything else.
    gl.enable(gl.BLEND);
    gl.depthMask(false);
    if (room && f.figures?.length) {
      gl.enable(gl.DEPTH_TEST);
      this.drawFigureMesh(f, f.ink, f.shows, 1, f.figures);
    }
  }

  // The kept picture, pixel for pixel (it is premultiplied, like the frame).
  private drawStill() {
    const { gl } = this;
    const p = this.blit;
    this.use(p);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.still.texture);
    gl.uniform1i(p.uniforms.uStill, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
    this.attribute(p, "aPos", 2, 2, 0);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
  }

  private drawOverlay(f: WorldFrame, quads: OverlayQuads) {
    if (quads.vertices === 0) return;
    const { gl } = this;
    const p = this.overlayProgram;
    this.use(p);
    this.uniformsFor(p, f);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.atlas.texture);
    gl.uniform1i(p.uniforms.uAtlas, 0);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, this.atlas.icons);
    gl.uniform1i(p.uniforms.uIcons, 1);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.trisBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, quads.data.subarray(0, quads.length), gl.STREAM_DRAW);
    this.attribute(p, "aPosUv", 4, overlayStride, 0);
    this.attribute(p, "aMode", 1, overlayStride, 4);
    this.attribute(p, "aColour", 4, overlayStride, 5);
    this.attribute(p, "aCircle", 4, overlayStride, 9);
    gl.drawArrays(gl.TRIANGLES, 0, quads.vertices);
  }

  private uniformsFor(p: Program, f: WorldFrame) {
    const { gl } = this;
    const s = f.shot;
    const yaw = (s.yaw * Math.PI) / 180;
    const pitch = (s.pitch * Math.PI) / 180;
    const u = p.uniforms;
    if (u.uCam) gl.uniform3f(u.uCam, s.x, s.y, s.z);
    if (u.uRot) gl.uniform4f(u.uRot, Math.cos(yaw), Math.sin(yaw), Math.cos(pitch), Math.sin(pitch));
    if (u.uLens) gl.uniform3f(u.uLens, s.focal, s.panini, s.squeeze);
    if (u.uView) gl.uniform4f(u.uView, s.cx, s.cy, f.width, f.height);
    if (u.uDpr) gl.uniform1f(u.uDpr, f.dpr);
    if (u.uFog) gl.uniform3f(u.uFog, f.fog[0] / 255, f.fog[1] / 255, f.fog[2] / 255);
    if (u.uShows) gl.uniform1f(u.uShows, f.shows);
  }

  private attribute(p: Program, name: string, size: number, stride: number, offset: number) {
    const { gl } = this;
    const at = p.attributes[name];
    if (at === undefined || at < 0) return;
    gl.enableVertexAttribArray(at);
    gl.vertexAttribPointer(at, size, gl.FLOAT, false, stride * 4, offset * 4);
  }

  private use(p: Program) {
    const { gl } = this;
    // Only this program's attributes are on (WebGL 1 has no vertex array objects by default).
    for (let i = 0; i < 8; i++) gl.disableVertexAttribArray(i);
    gl.useProgram(p.program);
  }

  private drawFaces(f: WorldFrame, buffer: WebGLBuffer, count: number, alpha: number) {
    if (count === 0) return;
    const { gl } = this;
    const p = this.faces;
    this.use(p);
    this.uniformsFor(p, f);
    gl.uniform1f(p.uniforms.uAlpha, alpha);
    gl.uniform3f(p.uniforms.uSeam, f.seam[0] / 255, f.seam[1] / 255, f.seam[2] / 255);
    gl.uniform3f(p.uniforms.uAverage, f.average[0] / 255, f.average[1] / 255, f.average[2] / 255);
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    this.attribute(p, "aPos", 3, faceStride, 0);
    this.attribute(p, "aColour", 3, faceStride, 3);
    this.attribute(p, "aUvKind", 3, faceStride, 6);
    gl.drawArrays(gl.TRIANGLES, 0, count);
  }

  private drawLines(f: WorldFrame) {
    if (this.counts.lines === 0) return;
    const { gl } = this;
    const p = this.lines;
    this.use(p);
    this.uniformsFor(p, f);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.roomLines);
    this.attribute(p, "aA", 3, lineStride, 0);
    this.attribute(p, "aB", 3, lineStride, 3);
    this.attribute(p, "aSideEndWidth", 3, lineStride, 6);
    this.attribute(p, "aColour", 4, lineStride, 9);
    gl.drawArrays(gl.TRIANGLES, 0, this.counts.lines);
  }

  // The figure mesh: a run of it (the shadows), or the parts listed by `order`.
  private drawFigureMesh(f: WorldFrame, colour: RGB, alphaScale: number, fogged: number, what: { first: number; count: number } | Uint16Array | Uint32Array) {
    if (this.counts.figures === 0) return;
    const { gl } = this;
    const p = this.figures;
    this.use(p);
    this.uniformsFor(p, f);
    gl.uniform3f(p.uniforms.uColour, colour[0] / 255, colour[1] / 255, colour[2] / 255);
    gl.uniform1f(p.uniforms.uAlphaScale, alphaScale);
    gl.uniform1f(p.uniforms.uFogged, fogged);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.figureVertices);
    this.attribute(p, "aPosKind", 4, figureStride, 0);
    this.attribute(p, "aABC", 3, figureStride, 4);
    this.attribute(p, "aOther", 3, figureStride, 7);
    this.attribute(p, "aPlayerAlpha", 3, figureStride, 10);
    if ("first" in what) {
      gl.drawArrays(gl.TRIANGLES, what.first, what.count);
      return;
    }
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.figureOrder);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, what, gl.STREAM_DRAW);
    gl.drawElements(gl.TRIANGLES, what.length, what instanceof Uint32Array ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT, 0);
  }

  private drawVignette(f: WorldFrame) {
    const { gl } = this;
    const p = this.vignette;
    this.use(p);
    gl.uniform4f(p.uniforms.uView, 0, 0, f.width, f.height);
    gl.uniform1f(p.uniforms.uDpr, f.dpr);
    const s = f.vignette.shade;
    gl.uniform4f(p.uniforms.uShade, s[0] / 255, s[1] / 255, s[2] / 255, f.vignette.alpha);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
    this.attribute(p, "aPos", 2, 2, 0);
    gl.drawArrays(gl.TRIANGLES, 0, 6);
  }

  private stencilBase = 0;

  // Groups (paths drawn once where they overlap themselves, see Tris.begin) use the stencil buffer:
  // each gets its own value, and a pixel already holding it is not drawn again. The rest is
  // drawn in as few calls as possible.
  private drawTris(f: WorldFrame, t: Tris) {
    if (t.vertices === 0) return;
    const { gl } = this;
    const p = this.tris;
    this.use(p);
    this.uniformsFor(p, f);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.trisBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, t.data.subarray(0, t.length), gl.STREAM_DRAW);
    this.attribute(p, "aPos", 2, trisStride, 0);
    this.attribute(p, "aColour", 4, trisStride, 2);
    this.attribute(p, "aCircle", 4, trisStride, 6);

    let at = 0;
    const plain = (until: number) => {
      if (until > at) {
        gl.disable(gl.STENCIL_TEST);
        gl.drawArrays(gl.TRIANGLES, at, until - at);
      }
    };
    for (const g of t.groups as Group[]) {
      plain(g.first);
      if (this.stencilBase >= 255) {
        gl.clear(gl.STENCIL_BUFFER_BIT);
        this.stencilBase = 0;
      }
      const id = ++this.stencilBase;
      gl.enable(gl.STENCIL_TEST);
      gl.stencilFunc(gl.NOTEQUAL, id, 0xff);
      gl.stencilOp(gl.KEEP, gl.KEEP, gl.REPLACE);
      gl.drawArrays(gl.TRIANGLES, g.first, g.count);
      at = g.first + g.count;
    }
    plain(t.vertices);
    gl.disable(gl.STENCIL_TEST);
  }
}

function program(gl: WebGLRenderingContext, vertex: string, fragment: string): Program {
  const shader = (type: number, source: string) => {
    const s = gl.createShader(type)!;
    gl.shaderSource(s, source);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(`shader: ${gl.getShaderInfoLog(s)}`);
    return s;
  };
  const p = gl.createProgram()!;
  const shaders = [shader(gl.VERTEX_SHADER, vertex), shader(gl.FRAGMENT_SHADER, fragment)];
  for (const s of shaders) gl.attachShader(p, s);
  gl.linkProgram(p);
  for (const s of shaders) gl.deleteShader(s); // the linked program keeps what it needs; they go with it
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(`program: ${gl.getProgramInfoLog(p)}`);
  const attributes: Record<string, number> = {};
  for (let i = 0; i < gl.getProgramParameter(p, gl.ACTIVE_ATTRIBUTES); i++) {
    const a = gl.getActiveAttrib(p, i)!;
    attributes[a.name] = gl.getAttribLocation(p, a.name);
  }
  const uniforms: Record<string, WebGLUniformLocation | null> = {};
  for (let i = 0; i < gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS); i++) {
    const u = gl.getActiveUniform(p, i)!;
    uniforms[u.name] = gl.getUniformLocation(p, u.name);
  }
  return { program: p, attributes, uniforms };
}
