import "../../../ui/src/styles/fonts.css";
import "../../../ui/src/styles/tokens.css";
import "./tracker.css";
import { FaceLandmarker, FilesetResolver } from "@mediapipe/tasks-vision";
import { getNativeFunction } from "@juce-framework/webview";

// Sends the pose to C++, which writes the shared head-tracking file
// (protocol: Source/stage/HeadTracking.h; yaw clockwise = head turned right).
const sendPose = getNativeFunction("pose") as (yaw: number, pitch: number, roll: number, face: boolean) => Promise<void>;

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const status = $("status");
const video = $<HTMLVideoElement>("video");
const settings = { invert: localStorage.getItem("invert") === "1", paused: false };
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
  });

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
    if (!matrix) {
      setStatus("No face", false);
      void sendPose(0, 0, 0, false);
      return;
    }

    const a = angles(matrix);
    lastRaw = (settings.invert ? -1 : 1) * a.yaw;
    const dt = Math.max(1e-3, (now - lastTime) / 1000);
    lastTime = now;
    const yaw = filter.filter(lastRaw - centre, dt);

    setStatus("Tracking", true);
    show(yaw);
    void sendPose(yaw, a.pitch, a.roll, true);
  };
  requestAnimationFrame(tick);

  setInterval(() => {
    $("stats").textContent = frames ? `${frames} fps · inference ${(inferenceMs / frames).toFixed(1)} ms` : "";
    frames = 0;
    inferenceMs = 0;
  }, 1000);
}

main().catch((e) => setStatus(`Error: ${e instanceof Error ? e.message : String(e)}`, false));
