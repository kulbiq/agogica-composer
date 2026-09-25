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
const setFilterParam = getNativeFunction("setFilterParam") as (name: string, value: number | null) => Promise<void>;

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

// ---- Stability parameters (tracker/engines/FilterTuner.h) --------------------------------------

type ParamState = { value: number; pinned: boolean };
const paramInfo: { name: string; label: string; min: number; max: number; step: number; format: (v: number) => string }[] = [
  { name: "confirmFrames", label: "Confirm frames", min: 1, max: 20, step: 1, format: (v) => `${Math.round(v)}` },
  { name: "reacquireSpeed", label: "Re-acquire speed", min: 20, max: 360, step: 5, format: (v) => `${Math.round(v)} °/s` },
  { name: "maxSpeed", label: "Max speed", min: 60, max: 720, step: 10, format: (v) => `${Math.round(v)} °/s` },
  { name: "turnSpeed", label: "Turn speed", min: 30, max: 500, step: 5, format: (v) => `${Math.round(v)} °/s` },
  { name: "sigmaZ", label: "Noise", min: 1, max: 20, step: 0.5, format: (v) => `${v.toFixed(1)}°` },
  { name: "flipWeight", label: "Front/back doubt", min: 0, max: 1, step: 0.01, format: (v) => `${Math.round(v * 100)} %` },
  { name: "outlierWeight", label: "Outliers", min: 0, max: 0.5, step: 0.01, format: (v) => `${Math.round(v * 100)} %` },
];

function buildParams() {
  const rows = new Map<string, { value: HTMLElement; auto: HTMLButtonElement; slider: HTMLInputElement }>();
  for (const info of paramInfo) {
    const row = document.createElement("div");
    row.className = "param";
    row.innerHTML = `<span class="label">${info.label}</span><span class="small"></span><button class="chip" aria-pressed="true">Auto</button>
      <input class="range" type="range" min="${info.min}" max="${info.max}" step="${info.step}" disabled />`;
    const value = row.querySelector<HTMLElement>(".small")!;
    const auto = row.querySelector<HTMLButtonElement>(".chip")!;
    const slider = row.querySelector<HTMLInputElement>(".range")!;
    auto.title = "Auto: adapted to the last seconds of tracking";
    auto.onclick = () => {
      const pinned = auto.getAttribute("aria-pressed") === "true"; // Auto -> pin the current value
      void setFilterParam(info.name, pinned ? Number(slider.value) : null);
    };
    slider.oninput = () => {
      value.textContent = info.format(Number(slider.value));
      void setFilterParam(info.name, Number(slider.value));
    };
    $("params").append(row);
    rows.set(info.name, { value, auto, slider });
  }

  return (params: Record<string, ParamState>) => {
    for (const info of paramInfo) {
      const state = params[info.name];
      const row = rows.get(info.name);
      if (!state || !row) continue;
      row.auto.setAttribute("aria-pressed", String(!state.pinned));
      row.slider.disabled = !state.pinned;
      if (document.activeElement !== row.slider) {
        row.slider.value = String(state.value);
        row.value.textContent = info.format(state.value);
      }
    }
  };
}

// ---- Plot: the last 10 s ------------------------------------------------------------------------

type PlotPoint = { t: number; raw?: number; confidence: number; estimate: number; output: number };
const plotPoints: PlotPoint[] = [];

function startPlot() {
  const canvas = $<HTMLCanvasElement>("plot");
  const ctx = canvas.getContext("2d")!;
  const ink = () => getComputedStyle(canvas).getPropertyValue("--ink-rgb").trim() || "15,23,42";

  const draw = () => {
    requestAnimationFrame(draw);
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = canvas.clientWidth, h = canvas.clientHeight;
    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);

    const rgb = ink();
    const now = performance.now();
    const x = (t: number) => w - ((now - t) / 10000) * w;
    const y = (deg: number) => h / 2 - (deg / 180) * (h / 2 - 6);

    // Grid: 0 (facing the camera), ±90, ±180.
    ctx.lineWidth = 1;
    for (const deg of [-180, -90, 0, 90, 180]) {
      ctx.strokeStyle = `rgba(${rgb},${deg === 0 ? 0.18 : 0.08})`;
      ctx.beginPath();
      ctx.moveTo(0, Math.round(y(deg)) + 0.5);
      ctx.lineTo(w, Math.round(y(deg)) + 0.5);
      ctx.stroke();
    }

    while (plotPoints.length && now - plotPoints[0].t > 10000) plotPoints.shift();

    for (const p of plotPoints)
      if (p.raw !== undefined) {
        ctx.fillStyle = `rgba(${rgb},${0.15 + 0.6 * p.confidence})`;
        ctx.fillRect(x(p.t) - 1, y(p.raw) - 1, 2, 2);
      }

    // Lines break where they wrap across ±180.
    const line = (key: "estimate" | "output", alpha: number, width: number) => {
      ctx.strokeStyle = `rgba(${rgb},${alpha})`;
      ctx.lineWidth = width;
      ctx.beginPath();
      plotPoints.forEach((p, i) => {
        const jump = i > 0 && Math.abs(p[key] - plotPoints[i - 1][key]) > 180;
        if (i === 0 || jump) ctx.moveTo(x(p.t), y(p[key]));
        else ctx.lineTo(x(p.t), y(p[key]));
      });
      ctx.stroke();
    };
    line("estimate", 0.35, 1);
    line("output", 0.9, 1.5);
  };
  requestAnimationFrame(draw);
}

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

  const showParams = buildParams();
  startPlot();

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

    // Plot in the same frame the listener hears (Invert and Recenter applied).
    const sign = settings.invert ? -1 : 1;
    const ev = e as unknown as { estimate?: number; confidence?: number; params?: Record<string, ParamState> };
    if (e.found)
      plotPoints.push({
        t: performance.now(),
        raw: e.measured && e.yaw !== undefined ? wrap(sign * e.yaw - centre) : undefined,
        confidence: ev.confidence ?? 1,
        estimate: wrap(sign * (ev.estimate ?? e.output) - centre),
        output: wrap(sign * e.output - centre),
      });
    if (ev.params) showParams(ev.params);
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
