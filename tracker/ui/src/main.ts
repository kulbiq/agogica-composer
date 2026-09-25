import "../../../ui/src/styles/fonts.css";
import "../../../ui/src/styles/tokens.css";
import "./tracker.css";
import { getNativeFunction } from "@juce-framework/webview";

// The app runs 6DRepNet360 natively (ONNX Runtime + CoreML) and sends raw estimates as
// "estimate" events. This page applies Invert / Recenter / smoothing / hold
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
  invert: localStorage.getItem(`invert:${engine}`) === "1",
};
let centre = 0;

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
// The app's temporal gate (tracker/engines/HeadFilter.h) already smooths, rejects the model's
// jumps and holds through short losses; here only Invert and Recenter apply (after the gate:
// the model's front/back error happens in the camera's frame).

class Output {
  private recent: { t: number; raw: number }[] = [];
  range = { min: 0, max: 0 };
  lastOutput = 0;
  frames = 0;
  dropouts = 0;

  feed(e: { found: boolean; output: number; measured?: boolean; yaw?: number; pitch?: number; roll?: number; reacquiring?: boolean }) {
    const now = performance.now();
    this.frames++;
    if (!e.measured) this.dropouts++;

    if (!e.found) {
      setStatus("No head", false);
      void sendPose(0, 0, 0, false);
      return;
    }

    const sign = settings.invert ? -1 : 1;
    if (e.measured && e.yaw !== undefined)
      this.recent = [...this.recent.filter((r) => now - r.t < 2000), { t: now, raw: sign * e.yaw }];

    this.lastOutput = sign * e.output;
    const yaw = wrap(this.lastOutput - centre);
    this.range.min = Math.min(this.range.min, yaw);
    this.range.max = Math.max(this.range.max, yaw);

    setStatus(e.reacquiring ? "Tracking (re-acquiring)" : e.measured ? "Tracking" : "Holding", true);
    show(yaw);
    void sendPose(yaw, e.pitch ?? 0, e.roll ?? 0, true);
  }

  // Frame-to-frame noise of the model's raw yaw over the last 2 s (hold still to read it).
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

  $("recenter").onclick = () => (centre = output.lastOutput);
  $("invert").setAttribute("aria-pressed", String(settings.invert));
  $("invert").onclick = () => {
    settings.invert = !settings.invert;
    localStorage.setItem(`invert:${engine}`, settings.invert ? "1" : "0");
    $("invert").setAttribute("aria-pressed", String(settings.invert));
  };
  $("pause").onclick = () => {
    settings.paused = !settings.paused;
    $("pause").setAttribute("aria-pressed", String(settings.paused));
    if (settings.paused) void sendPose(0, 0, 0, false);
  };

  let ms = 0;
  window.__JUCE__.backend.addEventListener("estimate", (payload) => {
    const e = payload as {
      status: string; detail?: string; provider?: string; found: boolean; output: number; measured?: boolean;
      yaw?: number; pitch?: number; roll?: number; box?: number[]; detectMs?: number; poseMs?: number; reacquiring?: boolean;
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
    if (e.measured && e.box) {
      const [x, y, w, h] = e.box;
      Object.assign(box.style, { left: `${(1 - x - w) * 100}%`, top: `${y * 100}%`, width: `${w * 100}%`, height: `${h * 100}%` });
      box.hidden = false;
    } else {
      box.hidden = true;
    }

    output.feed(e);
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
