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
const settings = {
  invert: localStorage.getItem("invert") === "1",
  paused: false,
  gain: Number(localStorage.getItem("gain") ?? 1),
};
let centre = 0;
let lastRaw = 0;

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

async function main() {
  $("recenter").onclick = () => (centre = lastRaw);
  $("invert").onclick = () => {
    settings.invert = !settings.invert;
    localStorage.setItem("invert", settings.invert ? "1" : "0");
    $("invert").setAttribute("aria-pressed", String(settings.invert));
  };
  $("invert").setAttribute("aria-pressed", String(settings.invert));
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

  setStatus("Opening camera", false);
  video.srcObject = await navigator.mediaDevices.getUserMedia({
    video: { width: 640, height: 480, frameRate: { ideal: 60 }, facingMode: "user" },
  });
  await video.play();

  setStatus("Loading model", false);
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

  // Past ~70-80 degrees the camera only sees a profile and loses the face. Near that edge we
  // hold the last angle (instead of letting Composer swing the scene back to the front);
  // lost near the centre means the listener left: release at once.
  const holdEdgeDegrees = 35;
  const holdSeconds = 3;
  let lastYaw = 0;
  let lostSince: number | undefined;
  const range = { min: 0, max: 0 };

  const poseLandmarker = await PoseLandmarker.createFromOptions(vision, {
    baseOptions: { modelAssetPath: "./models/pose_landmarker_lite.task", delegate: "GPU" },
    runningMode: "VIDEO",
    numPoses: 1,
    minPoseDetectionConfidence: 0.3,
    minPosePresenceConfidence: 0.3,
    minTrackingConfidence: 0.3,
  });
  const body = new BodyCalibration();
  // Filtered as a continuous angle (no jump at ±180° when facing away), wrapped on output.
  let unwrapped = 0;
  let lastWrapped = 0;
  let signedFace = 0; // the last face angle with the Invert setting applied, before Recenter
  let frame = 0;

  const filter = new OneEuro();
  let lastTime = performance.now();
  let frames = 0;
  let inferenceMs = 0;
  let lastVideoTime = -1;

  const tick = () => {
    requestAnimationFrame(tick);
    if (settings.paused || video.currentTime === lastVideoTime) return;
    lastVideoTime = video.currentTime;

    const now = performance.now();
    const started = performance.now();
    const result = landmarker.detectForVideo(video, now);
    inferenceMs += performance.now() - started;
    frames++;

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

    let source: "face" | "body" | undefined;
    if (face) {
      signedFace = (settings.invert ? -1 : 1) * face.yaw;
      lastRaw = signedFace;
      if (bodyRaw !== undefined && Math.abs(signedFace) < 50) body.learn(signedFace, bodyRaw);
      source = "face";
    } else if (bodyRaw !== undefined && body.samples > 30) {
      // Continue from where the face left off, in the face's frame.
      signedFace = body.map(bodyRaw, signedFace);
      lastRaw = signedFace;
      source = "body";
    }

    $("source").textContent =
      `Source: ${source ?? "none"}` + (bodyRaw !== undefined ? ` · body ${Math.round(bodyRaw)}° (${body.samples > 30 ? "calibrated" : "learning"})` : "");

    if (!source) {
      lostSince ??= now;
      const holding = Math.abs(lastYaw) >= holdEdgeDegrees && now - lostSince < holdSeconds * 1000;
      setStatus(holding ? "Holding (turned away)" : "No face", holding);
      void sendPose(holding ? lastYaw : 0, 0, 0, holding);
      return;
    }
    lostSince = undefined;

    const dt = Math.max(1e-3, (now - lastTime) / 1000);
    lastTime = now;
    const wrapped = wrap(signedFace - centre);
    unwrapped += wrap(wrapped - lastWrapped);
    lastWrapped = wrapped;
    const head = wrap(filter.filter(unwrapped, dt));
    range.min = Math.min(range.min, head);
    range.max = Math.max(range.max, head);
    $("range").textContent = `Tracked this session: ${Math.round(range.min)}° … ${Math.round(range.max)}°`;

    // Rotation gain: e.g. x2 turns 60 degrees of head into 120 degrees of scene.
    const yaw = wrap(head * settings.gain);
    lastYaw = yaw;

    setStatus(source === "face" ? "Tracking" : "Tracking (body)", true);
    show(yaw);
    void sendPose(yaw, face?.pitch ?? 0, face?.roll ?? 0, true);
  };
  requestAnimationFrame(tick);

  setInterval(() => {
    $("stats").textContent = frames ? `${frames} fps · inference ${(inferenceMs / frames).toFixed(1)} ms` : "";
    frames = 0;
    inferenceMs = 0;
  }, 1000);
}

main().catch((e) => setStatus(`Error: ${e instanceof Error ? e.message : String(e)}`, false));
