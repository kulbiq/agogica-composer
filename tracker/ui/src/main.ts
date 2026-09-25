import "../../../ui/src/styles/fonts.css";
import "../../../ui/src/styles/tokens.css";
import "./tracker.css";
import { getNativeFunction } from "@juce-framework/webview";

// The app runs 6DRepNet360 natively (ONNX Runtime + CoreML) and sends raw estimates as
// "estimate" events. This page applies Invert / Recenter / rotation gain / smoothing / hold
// and writes the pose to Composer (protocol: Source/stage/HeadTracking.h; yaw clockwise =
// head turned right).
const sendPose = getNativeFunction("pose") as (yaw: number, pitch: number, roll: number, face: boolean) => Promise<void>;
const setEngine = getNativeFunction("setEngine") as (id: string) => Promise<void>;

const engine = "6drepnet360";
const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const status = $("status");
const video = $<HTMLVideoElement>("video");
const settings = {
  paused: false,
  gain: Number(localStorage.getItem("gain") ?? 1),
  invert: localStorage.getItem(`invert:${engine}`) === "1",
};
let centre = 0;

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

const wrap = (a: number) => ((((a + 180) % 360) + 360) % 360) - 180;

function setStatus(text: string, live: boolean) {
  status.textContent = text;
  status.dataset.status = live ? "live" : "soon";
}

function show(yaw: number) {
  $("yaw").textContent = `${yaw < 0 ? "−" : ""}${Math.abs(yaw).toFixed(1)}°`;
  $("head").setAttribute("transform", `rotate(${yaw})`);
}

// ---- Output ----------------------------------------------------------------------------------
// Past its range the model loses the head. Near the edge we hold the last angle (instead of
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

  // `raw`: the yaw with Invert applied, before Recenter; undefined = lost.
  feed(raw: number | undefined, pitch: number, roll: number) {
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
    const wrapped = wrap(raw - centre);
    this.unwrapped += wrap(wrapped - this.lastWrapped);
    this.lastWrapped = wrapped;
    const head = wrap(this.filter.filter(this.unwrapped, dt));
    this.range.min = Math.min(this.range.min, head);
    this.range.max = Math.max(this.range.max, head);

    // Rotation gain: e.g. x2 turns 60 degrees of head into 120 degrees of scene.
    const yaw = wrap(head * settings.gain);
    this.lastYaw = yaw;

    setStatus("Tracking", true);
    show(yaw);
    void sendPose(yaw, pitch, roll, true);
  }

  // Frame-to-frame noise of the raw yaw over the last 2 s (hold still to read the jitter).
  jitter() {
    const d = this.recent.slice(1).map((r, i) => wrap(r.raw - this.recent[i].raw));
    return d.length ? Math.sqrt(d.reduce((a, b) => a + b * b, 0) / d.length) : 0;
  }
}

const output = new Output();

async function main() {
  // The camera section stays as the user left it.
  const camera = document.querySelector<HTMLDetailsElement>("details.more")!;
  camera.open = localStorage.getItem("cameraOpen") === "1";
  camera.ontoggle = () => localStorage.setItem("cameraOpen", camera.open ? "1" : "0");

  $("recenter").onclick = () => (centre = output.lastRaw);
  $("invert").setAttribute("aria-pressed", String(settings.invert));
  $("invert").onclick = () => {
    settings.invert = !settings.invert;
    localStorage.setItem(`invert:${engine}`, settings.invert ? "1" : "0");
    $("invert").setAttribute("aria-pressed", String(settings.invert));
  };
  const gain = $<HTMLInputElement>("gain");
  const showGain = () => ($("gainValue").textContent = `×${settings.gain.toFixed(1)}`);
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

  let ms = 0;
  window.__JUCE__.backend.addEventListener("estimate", (payload) => {
    const e = payload as {
      status: string; detail?: string; provider?: string; found?: boolean;
      yaw?: number; pitch?: number; roll?: number; box?: number[]; detectMs?: number; poseMs?: number;
    };
    if (settings.paused) return;

    if (e.status !== "running") {
      const text = { loading: "Loading model", "starting camera": "Starting camera", compiling: `Compiling for ${e.detail}`, error: `Error: ${e.detail}` }[e.status];
      setStatus(text ?? e.status, false);
      return;
    }

    ms = (e.detectMs ?? 0) + (e.poseMs ?? 0);
    $("source").textContent = `${e.provider} · detect ${(e.detectMs ?? 0).toFixed(1)} ms · pose ${(e.poseMs ?? 0).toFixed(1)} ms`;

    const box = $("box");
    if (e.found && e.box) {
      const [x, y, w, h] = e.box;
      Object.assign(box.style, { left: `${(1 - x - w) * 100}%`, top: `${y * 100}%`, width: `${w * 100}%`, height: `${h * 100}%` });
      box.hidden = false;
    } else {
      box.hidden = true;
    }

    const raw = e.found && e.yaw !== undefined ? (settings.invert ? -1 : 1) * e.yaw : undefined;
    output.feed(raw, e.pitch ?? 0, e.roll ?? 0);
  });

  setStatus("Loading", false);
  void setEngine(engine);

  // The preview (the app reads the camera natively; macOS lets both run).
  video.srcObject = await navigator.mediaDevices.getUserMedia({ video: { width: 640, height: 480, facingMode: "user" } });
  await video.play();

  setInterval(() => {
    $("stats").textContent = output.frames
      ? `${output.frames} fps · ${ms.toFixed(1)} ms · jitter ${output.jitter().toFixed(2)}° · dropouts ${output.dropouts}`
      : "";
    $("range").textContent = `Tracked this session: ${Math.round(output.range.min)}° … ${Math.round(output.range.max)}°`;
    output.frames = 0;
  }, 1000);
}

main().catch((e) => setStatus(`Error: ${e instanceof Error ? e.message : String(e)}`, false));
