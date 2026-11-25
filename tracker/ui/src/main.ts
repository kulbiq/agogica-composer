import "../../../ui/src/styles/fonts.css";
import "../../../ui/src/styles/tokens.css";
import "./tracker.css";
import { getNativeFunction } from "@juce-framework/webview";

// The app runs HFHPE natively (ONNX Runtime) and writes the pose for Composer itself
// (tracker/PoseOutput.h: Invert, Recenter and Pause on each frame's reading as it is, no
// smoothing; yaw clockwise = head turned right). This page shows it: "pose" events (60 Hz) turn
// the dial, "estimate" events (one per camera frame) carry the status, readouts and plot.
const getInvert = getNativeFunction("getInvert") as () => Promise<boolean>;
const setInvert = getNativeFunction("setInvert") as (on: boolean) => Promise<void>;
const recenter = getNativeFunction("recenter") as () => Promise<void>;
const setPaused = getNativeFunction("setPaused") as (on: boolean) => Promise<void>;
const setPreview = getNativeFunction("setPreview") as (on: boolean) => Promise<void>;
const start = getNativeFunction("start") as () => Promise<void>;
const getDebug = getNativeFunction("getDebug") as () => Promise<boolean>;
const fitHeight = getNativeFunction("fitHeight") as (height: number) => Promise<void>;

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const status = $("status");
const frame = $<HTMLImageElement>("frame");
const settings = { paused: false, invert: false }; // invert: read from the app in main()
let centre = 0; // the plot's frame (the app keeps its own for Composer)

const wrap = (a: number) => ((((a + 180) % 360) + 360) % 360) - 180;

function setStatus(text: string, live: boolean) {
  status.textContent = text;
  status.dataset.status = live ? "live" : "soon";
}

function show(yaw: number) {
  $("head").setAttribute("transform", `rotate(${yaw})`);
}

// The dial's grid: a tick every 30 degrees (longer every 90) inside the ring, each labelled in
// Composer's convention (clockwise positive = head turned right; 180 behind).
function buildGrid() {
  const svg = "http://www.w3.org/2000/svg";
  const grid = $("grid");
  for (let a = -150; a <= 180; a += 30) {
    const r = (a * Math.PI) / 180, sx = Math.sin(r), cy = -Math.cos(r);
    const major = a % 90 === 0;
    const tick = document.createElementNS(svg, "line");
    tick.setAttribute("class", major ? "tick major" : "tick");
    const inner = major ? 44 : 46.5;
    tick.setAttribute("x1", String(sx * inner));
    tick.setAttribute("y1", String(cy * inner));
    tick.setAttribute("x2", String(sx * 50));
    tick.setAttribute("y2", String(cy * 50));
    const label = document.createElementNS(svg, "text");
    label.setAttribute("class", "angle");
    label.setAttribute("x", String(sx * 38.5));
    label.setAttribute("y", String(cy * 38.5));
    label.textContent = a < 0 ? `−${-a}` : String(a);
    grid.append(tick, label);
  }
}

// ---- Output ----------------------------------------------------------------------------------
// The app applies Invert and Recenter to each frame's reading and writes the pose (no filter).
// Here: the status and readouts per camera frame.

class Output {
  private recent: { t: number; raw: number }[] = [];
  range = { min: 0, max: 0 };
  lastOutput = 0;
  frames = 0;
  dropouts = 0;

  feed(e: { found: boolean; yaw?: number }) {
    const now = performance.now();
    this.frames++;

    if (!e.found || e.yaw === undefined) {
      this.dropouts++;
      setStatus("No head", false);
      return;
    }

    const sign = settings.invert ? -1 : 1;
    this.recent = [...this.recent.filter((r) => now - r.t < 2000), { t: now, raw: sign * e.yaw }];
    this.lastOutput = sign * e.yaw;
    setStatus("Tracking", true);
  }

  // What Composer hears (the app's "pose" event).
  pose(yaw: number) {
    this.range.min = Math.min(this.range.min, yaw);
    this.range.max = Math.max(this.range.max, yaw);
    show(yaw);
  }

  // Frame-to-frame noise of the model's raw yaw over the last 2 s (hold still to read it).
  jitter() {
    const d = this.recent.slice(1).map((r, i) => wrap(r.raw - this.recent[i].raw));
    return d.length ? Math.sqrt(d.reduce((a, b) => a + b * b, 0) / d.length) : 0;
  }
}

const output = new Output();

// ---- Plot: the last 10 s ------------------------------------------------------------------------

type PlotPoint = { t: number; yaw: number; confidence: number };
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

    for (const p of plotPoints) {
      ctx.fillStyle = `rgba(${rgb},${0.2 + 0.7 * p.confidence})`;
      ctx.fillRect(x(p.t) - 1.5, y(p.yaw) - 1.5, 3, 3);
    }
  };
  requestAnimationFrame(draw);
}

// Debugging (the app's View menu) shows the readouts and the plot; off, the window
// is fixed and fitted to what's left.
function setDebug(on: boolean) {
  document.body.toggleAttribute("data-debug", on);
  if (!on) void fitHeight(document.querySelector<HTMLElement>(".app")!.offsetHeight);
}

async function main() {
  setDebug(await getDebug());
  window.__JUCE__.backend.addEventListener("debug", (on) => setDebug(on as boolean));
  // Opening or closing the camera changes the height.
  new ResizeObserver(() => setDebug(document.body.hasAttribute("data-debug"))).observe(document.querySelector(".app")!);

  // The camera section stays as the user left it.
  // The preview: frames from the app's own camera (a second reader isn't allowed on Windows),
  // sent only while the section is open.
  const camera = document.querySelector<HTMLDetailsElement>("details.more")!;
  camera.open = localStorage.getItem("cameraOpen") === "1";
  camera.ontoggle = () => {
    localStorage.setItem("cameraOpen", camera.open ? "1" : "0");
    void setPreview(camera.open);
  };
  void setPreview(camera.open);
  window.__JUCE__.backend.addEventListener("frame", (src) => {
    frame.src = src as string;
    frame.hidden = false;
  });
  buildGrid();

  window.__JUCE__.backend.addEventListener("about", (version) => {
    $("version").textContent = `Version ${version as string}`;
    document.body.toggleAttribute("data-about", true);
  });
  $("aboutBack").onclick = () => document.body.toggleAttribute("data-about", false);

  settings.invert = await getInvert();

  $("recenter").onclick = () => {
    centre = output.lastOutput;
    void recenter();
  };
  $("invert").setAttribute("aria-pressed", String(settings.invert));
  $("invert").onclick = () => {
    settings.invert = !settings.invert;
    void setInvert(settings.invert);
    $("invert").setAttribute("aria-pressed", String(settings.invert));
  };
  $("pause").onclick = () => {
    settings.paused = !settings.paused;
    $("pause").setAttribute("aria-pressed", String(settings.paused));
    void setPaused(settings.paused);
  };
  window.__JUCE__.backend.addEventListener("pose", (payload) => {
    const p = payload as { yaw: number; live: boolean };
    if (p.live && !settings.paused) output.pose(p.yaw);
  });

  startPlot();

  let ms = 0;
  window.__JUCE__.backend.addEventListener("estimate", (payload) => {
    const e = payload as {
      status: string; detail?: string; provider?: string; found: boolean; yaw?: number; pitch?: number; roll?: number;
      confidence?: number; box?: number[]; detectMs?: number; poseMs?: number;
    };
    if (settings.paused) return;

    if (e.status !== "running") {
      const text = { loading: "Loading model", "starting camera": "Starting camera", compiling: `Compiling for ${e.detail}`, error: `Error: ${e.detail}` }[e.status];
      setStatus(text ?? e.status, false);
      return;
    }

    // Timings come with frames the models ran on (a still head repeats the last reading).
    if (e.detectMs !== undefined) {
      ms = e.detectMs + (e.poseMs ?? 0);
      $("source").textContent = `${e.provider} · detect ${e.detectMs.toFixed(1)} ms · pose ${(e.poseMs ?? 0).toFixed(1)} ms`;
    }

    const box = $("box");
    if (e.found && e.box) {
      const [x, y, w, h] = e.box;
      Object.assign(box.style, { left: `${(1 - x - w) * 100}%`, top: `${y * 100}%`, width: `${w * 100}%`, height: `${h * 100}%` });
      box.hidden = false;
    } else {
      box.hidden = true;
    }

    output.feed(e);

    // Plot in the same frame the listener hears (Invert and Recenter applied).
    const sign = settings.invert ? -1 : 1;
    if (e.found && e.yaw !== undefined)
      plotPoints.push({ t: performance.now(), yaw: wrap(sign * e.yaw - centre), confidence: e.confidence ?? 1 });

    // Monitoring, once a second: the camera, the app's CPU (all threads) and where camera frames went.
    const perf = (e as { perf?: { camera: string; fps: number; cpu: number; models: number; reused: number; dropped: number } }).perf;
    if (perf)
      $("perf").textContent = `Camera ${perf.camera} at ${perf.fps.toFixed(0)}/s · CPU ${perf.cpu.toFixed(2)} cores · models ${perf.models.toFixed(0)}/s · reused ${perf.reused.toFixed(0)}/s · dropped ${perf.dropped.toFixed(0)}/s`;
  });

  setStatus("Loading", false);
  void start();

  setInterval(() => {
    $("stats").textContent = output.frames
      ? `${output.frames} fps · ${ms.toFixed(1)} ms · jitter ${output.jitter().toFixed(2)}° · dropouts ${output.dropouts}`
      : "";
    $("range").textContent = `Tracked this session: ${Math.round(output.range.min)}° … ${Math.round(output.range.max)}°`;
    output.frames = 0;
  }, 1000);
}

main().catch((e) => setStatus(`Error: ${e instanceof Error ? e.message : String(e)}`, false));
