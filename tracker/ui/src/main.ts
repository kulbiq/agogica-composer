import "../../../ui/src/styles/fonts.css";
import "../../../ui/src/styles/tokens.css";
import "./tracker.css";
import { FaceLandmarker, FilesetResolver, PoseLandmarker } from "@mediapipe/tasks-vision";
import { getNativeFunction } from "@juce-framework/webview";

// Sends the pose to C++, which writes the shared head-tracking file
// (protocol: Source/stage/HeadTracking.h; yaw clockwise = head turned right).
const sendPose = getNativeFunction("pose") as (yaw: number, pitch: number, roll: number, face: boolean) => Promise<void>;

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const status = $("status");
const video = $<HTMLVideoElement>("video");
const settings = { paused: false, gain: Number(localStorage.getItem("gain") ?? 1) };

// One-Euro filter (Casiez et al. 2012): smooth when still, responsive when moving.
class OneEuro {
  private x?: number;
  private dx = 0;
  constructor(private minCutoff = 1.0, private beta = 0.02, private dCutoff = 1.0) {}
  private static alpha(cutoff: number, dt: number) {
    const tau = 1 / (2 * Math.PI * cutoff);
    return 1 / (1 + tau / dt);
  }
  filter(value: number, dt: number) {
    if (this.x === undefined) return (this.x = value);
    const dx = (value - this.x) / dt;
    this.dx += OneEuro.alpha(this.dCutoff, dt) * (dx - this.dx);
    const cutoff = this.minCutoff + this.beta * Math.abs(this.dx);
    return (this.x += OneEuro.alpha(cutoff, dt) * (value - this.x));
  }
}

// Head angles from the facial transformation matrix (column-major 4x4, rotation part):
// yaw about the vertical axis, pitch about the ear axis, roll about the nose axis. Degrees.
function angles(m: ArrayLike<number>) {
  const r = (row: number, col: number) => m[col * 4 + row];
  const deg = 180 / Math.PI;
  return {
    yaw: Math.atan2(r(0, 2), r(2, 2)) * deg,
    pitch: Math.asin(Math.max(-1, Math.min(1, -r(1, 2)))) * deg,
    roll: Math.atan2(r(1, 0), r(1, 1)) * deg,
  };
}

// ---- Body posture: the head's direction from the ear axis --------------------------------
// The face model only works while it sees a face (roughly ±70-80°). The pose model finds the
// body from any side, and its 3D "world" landmarks include both ears, so the ear axis gives a
// direction all the way round - less precise, and it can swap left and right from behind.

const wrap = (a: number) => ((((a + 180) % 360) + 360) % 360) - 180;

// Raw ear-axis angle (degrees): 0 when both ears are equally far from the camera.
function earAxis(world: { x: number; y: number; z: number }[]) {
  const left = world[7];
  const right = world[8];
  return (Math.atan2(right.z - left.z, -(right.x - left.x)) * 180) / Math.PI;
}

// Learns how the ear-axis angle maps onto the face angle while both are visible (sign and
// offset), so the hand-over when the face is lost is seamless.
class BodyCalibration {
  private correlation = 0;
  private offsetSin = 0;
  private offsetCos = 0;
  samples = 0;

  get sign() {
    return this.correlation >= 0 ? 1 : -1;
  }

  learn(face: number, body: number) {
    const k = 0.05;
    this.correlation += k * (Math.sin((face * Math.PI) / 180) * Math.sin((body * Math.PI) / 180) - this.correlation);
    const offset = ((face - this.sign * body) * Math.PI) / 180;
    this.offsetSin += k * (Math.sin(offset) - this.offsetSin);
    this.offsetCos += k * (Math.cos(offset) - this.offsetCos);
    this.samples++;
  }

  // The body angle in the face's frame, taking whichever of the two left/right readings is
  // closer to where the head was (it can't jump 180° between frames).
  map(body: number, previous: number) {
    const offset = (Math.atan2(this.offsetSin, this.offsetCos) * 180) / Math.PI;
    const a = wrap(this.sign * body + offset);
    const b = wrap(a + 180);
    return Math.abs(wrap(a - previous)) <= Math.abs(wrap(b - previous)) ? a : b;
  }
}

function setStatus(text: string, live: boolean) {
  status.textContent = text;
  status.dataset.status = live ? "live" : "soon";
}

function show(yaw: number) {
  $("yaw").textContent = `${yaw < 0 ? "−" : ""}${Math.abs(yaw).toFixed(1)}°`;
  $("head").setAttribute("transform", `rotate(${yaw})`);
}

// ---- Engines ------------------------------------------------------------------------------
// MediaPipe runs here in the page; the others run natively in the app (ONNX Runtime + CoreML)
// and send raw estimates as "estimate" events. Every engine's yaw goes through the same
// Output pipeline below, so they're compared on their own quality, not different smoothing.

type EngineId = "mediapipe" | "whenet" | "6drepnet360" | "deimv2" | "yolov8pose";

const engineNotes: Record<EngineId, string> = {
  mediapipe: "MediaPipe face + body posture · Apache 2.0",
  whenet: "WHENet, full-range yaw from a head crop · BSD-3 · trained on non-commercial data",
  "6drepnet360": "6DRepNet360, head crop, heading from the full rotation · MIT · trained on non-commercial data",
  deimv2: "DEIMv2 head direction, 8 classes (45° steps) · Apache 2.0",
  yolov8pose: "YOLOv8n-Pose, coarse yaw from nose / ears / shoulders · AGPL-3.0",
};

const setEngineNative = getNativeFunction("setEngine") as (id: string) => Promise<void>;
// ?engine= (development: AGOGICA_TRACKER_ENGINE) overrides the remembered choice.
let engine = (new URLSearchParams(location.search).get("engine") ?? localStorage.getItem("engine") ?? "mediapipe") as EngineId;

// Sign and straight-ahead differ per model: remembered per engine.
const invertOf = (id: EngineId) => localStorage.getItem(`invert:${id}`) === "1";
const centres: Partial<Record<EngineId, number>> = {};

// ---- Output: the same for every engine -------------------------------------------------------
// Past its range an engine loses the head. Near the edge we hold the last angle (instead of
// swinging the scene back to the front); lost near the centre means the listener left.
const holdEdgeDegrees = 35;
const holdSeconds = 3;

class Output {
  private filter = new OneEuro();
  private unwrapped = 0;
  private lastWrapped = 0;
  private lastTime = performance.now();
  private lastYaw = 0;
  private lostSince?: number;
  private recent: { t: number; raw: number }[] = [];
  range = { min: 0, max: 0 };
  lastRaw = 0;
  frames = 0;
  dropouts = 0;

  // `raw`: the engine's yaw with its Invert applied, before Recenter; undefined = lost.
  feed(raw: number | undefined, pitch: number, roll: number, label: string) {
    const now = performance.now();
    this.frames++;

    if (raw === undefined) {
      this.dropouts++;
      this.lostSince ??= now;
      const holding = Math.abs(this.lastYaw) >= holdEdgeDegrees && now - this.lostSince < holdSeconds * 1000;
      setStatus(holding ? "Holding (turned away)" : "No head", holding);
      void sendPose(holding ? this.lastYaw : 0, 0, 0, holding);
      return;
    }
    this.lostSince = undefined;
    this.lastRaw = raw;
    this.recent = [...this.recent.filter((r) => now - r.t < 2000), { t: now, raw }];

    const dt = Math.max(1e-3, (now - this.lastTime) / 1000);
    this.lastTime = now;
    const wrapped = wrap(raw - (centres[engine] ?? 0));
    this.unwrapped += wrap(wrapped - this.lastWrapped);
    this.lastWrapped = wrapped;
    const head = wrap(this.filter.filter(this.unwrapped, dt));
    this.range.min = Math.min(this.range.min, head);
    this.range.max = Math.max(this.range.max, head);

    // Rotation gain: e.g. x2 turns 60 degrees of head into 120 degrees of scene.
    const yaw = wrap(head * settings.gain);
    this.lastYaw = yaw;

    setStatus(`Tracking${label ? ` (${label})` : ""}`, true);
    show(yaw);
    void sendPose(yaw, pitch, roll, true);
  }

  // Frame-to-frame noise of the raw yaw over the last 2 s (hold still to read the jitter).
  jitter() {
    const d = this.recent.slice(1).map((r, i) => wrap(r.raw - this.recent[i].raw));
    return d.length ? Math.sqrt(d.reduce((a, b) => a + b * b, 0) / d.length) : 0;
  }
}

let output = new Output();

async function main() {
  // The camera section stays as the user left it.
  const camera = document.querySelector<HTMLDetailsElement>("details.more")!;
  camera.open = localStorage.getItem("cameraOpen") === "1";
  camera.ontoggle = () => localStorage.setItem("cameraOpen", camera.open ? "1" : "0");

  const select = $<HTMLSelectElement>("engine");
  const syncInvert = () => $("invert").setAttribute("aria-pressed", String(invertOf(engine)));

  const choose = (id: EngineId) => {
    engine = id;
    localStorage.setItem("engine", id);
    select.value = id;
    $("engineNote").textContent = engineNotes[id];
    $("box").hidden = true;
    output = new Output();
    syncInvert();
    setStatus(id === "mediapipe" ? "Starting" : "Loading", false);
    void setEngineNative(id);
  };
  select.onchange = () => choose(select.value as EngineId);

  $("recenter").onclick = () => (centres[engine] = output.lastRaw);
  $("invert").onclick = () => {
    localStorage.setItem(`invert:${engine}`, invertOf(engine) ? "0" : "1");
    syncInvert();
  };
  const gain = $<HTMLInputElement>("gain");
  const showGain = () => ($("gainValue").textContent = `\u00d7${settings.gain.toFixed(1)}`);
  gain.value = String(settings.gain);
  showGain();
  gain.oninput = () => {
    settings.gain = Number(gain.value);
    localStorage.setItem("gain", gain.value);
    showGain();
  };
  $("pause").onclick = () => {
    settings.paused = !settings.paused;
    $("pause").setAttribute("aria-pressed", String(settings.paused));
    if (settings.paused) void sendPose(0, 0, 0, false);
  };

  // Native engines: raw estimates from the app.
  let nativeMs = 0;
  window.__JUCE__.backend.addEventListener("estimate", (payload) => {
    const e = payload as {
      engine: EngineId; status: string; detail?: string; provider?: string; found?: boolean;
      yaw?: number; pitch?: number; roll?: number; box?: number[]; detectMs?: number; poseMs?: number;
    };
    if (e.engine !== engine || settings.paused) return;

    if (e.status !== "running") {
      const text = { loading: "Loading model", "starting camera": "Starting camera", compiling: `Compiling for ${e.detail}`, error: `Error: ${e.detail}` }[e.status];
      setStatus(text ?? e.status, false);
      return;
    }

    nativeMs = (e.detectMs ?? 0) + (e.poseMs ?? 0);
    $("source").textContent = `${e.provider} · detect ${(e.detectMs ?? 0).toFixed(1)} ms · pose ${(e.poseMs ?? 0).toFixed(1)} ms`;

    const box = $("box");
    if (e.found && e.box) {
      const [x, y, w, h] = e.box;
      Object.assign(box.style, { left: `${(1 - x - w) * 100}%`, top: `${y * 100}%`, width: `${w * 100}%`, height: `${h * 100}%` });
      box.hidden = false;
    } else {
      box.hidden = true;
    }

    const raw = e.found && e.yaw !== undefined ? (invertOf(engine) ? -1 : 1) * e.yaw : undefined;
    output.feed(raw, e.pitch ?? 0, e.roll ?? 0, "");
  });

  setStatus("Opening camera", false);
  video.srcObject = await navigator.mediaDevices.getUserMedia({
    video: { width: 640, height: 480, frameRate: { ideal: 60 }, facingMode: "user" },
  });
  await video.play();

  // MediaPipe (runs only while it's the chosen engine).
  const vision = await FilesetResolver.forVisionTasks("./wasm");
  const landmarker = await FaceLandmarker.createFromOptions(vision, {
    baseOptions: { modelAssetPath: "./models/face_landmarker.task", delegate: "GPU" },
    runningMode: "VIDEO",
    numFaces: 1,
    outputFacialTransformationMatrixes: true,
    // Below MediaPipe's 0.5 defaults: keeps the face locked further into profile.
    minFaceDetectionConfidence: 0.3,
    minFacePresenceConfidence: 0.3,
    minTrackingConfidence: 0.3,
  });
  const poseLandmarker = await PoseLandmarker.createFromOptions(vision, {
    baseOptions: { modelAssetPath: "./models/pose_landmarker_lite.task", delegate: "GPU" },
    runningMode: "VIDEO",
    numPoses: 1,
    minPoseDetectionConfidence: 0.3,
    minPosePresenceConfidence: 0.3,
    minTrackingConfidence: 0.3,
  });
  const body = new BodyCalibration();
  let signed = 0; // the last MediaPipe angle with Invert applied, before Recenter
  let frame = 0;
  let mediapipeMs = 0;
  let lastVideoTime = -1;

  choose(engine);

  const tick = () => {
    requestAnimationFrame(tick);
    if (engine !== "mediapipe" || settings.paused || video.currentTime === lastVideoTime) return;
    lastVideoTime = video.currentTime;

    const now = performance.now();
    const started = performance.now();
    const result = landmarker.detectForVideo(video, now);
    const matrix = result.facialTransformationMatrixes?.[0]?.data;
    const face = matrix ? angles(matrix) : undefined;
    frame++;

    // The body model runs every frame once the face is lost, and every 3rd frame while it's
    // visible (to keep learning the calibration).
    let bodyRaw: number | undefined;
    if (!face || frame % 3 === 0) {
      const world = poseLandmarker.detectForVideo(video, now).worldLandmarks?.[0];
      if (world) bodyRaw = earAxis(world);
    }
    mediapipeMs = performance.now() - started;

    let source: "face" | "body" | undefined;
    if (face) {
      signed = (invertOf("mediapipe") ? -1 : 1) * face.yaw;
      if (bodyRaw !== undefined && Math.abs(signed) < 50) body.learn(signed, bodyRaw);
      source = "face";
    } else if (bodyRaw !== undefined && body.samples > 30) {
      // Continue from where the face left off, in the face's frame.
      signed = body.map(bodyRaw, signed);
      source = "body";
    }

    $("source").textContent =
      `Source: ${source ?? "none"}` + (bodyRaw !== undefined ? ` · body ${Math.round(bodyRaw)}° (${body.samples > 30 ? "calibrated" : "learning"})` : "");
    $("box").hidden = true;
    output.feed(source ? signed : undefined, face?.pitch ?? 0, face?.roll ?? 0, source === "body" ? "body" : "");
  };
  requestAnimationFrame(tick);

  setInterval(() => {
    const ms = engine === "mediapipe" ? mediapipeMs : nativeMs;
    $("stats").textContent = output.frames
      ? `${output.frames} fps · ${ms.toFixed(1)} ms · jitter ${output.jitter().toFixed(2)}° · dropouts ${output.dropouts}`
      : "";
    $("range").textContent = `Tracked this session: ${Math.round(output.range.min)}° … ${Math.round(output.range.max)}°`;
    output.frames = 0;
  }, 1000);
}

main().catch((e) => setStatus(`Error: ${e instanceof Error ? e.message : String(e)}`, false));
