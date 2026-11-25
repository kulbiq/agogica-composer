import "../../../ui/src/styles/fonts.css";
import "../../../ui/src/styles/tokens.css";
import "./tracker.css";
import { fitHeight, onEvent, pageReady, recenter, setInverted, setPaused, type TrackerStatus } from "./bridge";
import { setUpCameraSection, showCameraPicture, showHeadBox } from "./cameraPreview";
import { buildDialGrid, turnDialTo } from "./dial";
import { elementById, wrapDegrees } from "./dom";
import { startPlot } from "./plot";
import { SessionStats } from "./sessionStats";

// The app tracks the head and writes the pose for Composer by itself; this page shows it and
// sends the buttons. "listenerPose" events turn the dial, "trackingFrame" events (one per camera
// frame) carry the readouts and the plot.

const STATUS_TEXT: Record<TrackerStatus["phase"], (detail: string) => string> = {
  loadingModels: () => "Loading model",
  startingCamera: () => "Starting camera",
  compilingModels: (provider) => `Compiling for ${provider}`,
  tracking: () => "Tracking",
  failed: (reason) => `Error: ${reason}`,
};

function showStatus(text: string, isLive: boolean) {
  const chip = elementById("status");
  chip.textContent = text;
  chip.dataset.status = isLive ? "live" : "soon";
}

// Debugging (the app's View menu) shows the readouts and the plot; off, the window is fixed and
// the app fits it to the page.
function showDebugView(on: boolean) {
  document.body.toggleAttribute("data-debug", on);
  fitWindowToPage();
}

function fitWindowToPage() {
  const app = document.querySelector<HTMLElement>(".app");
  if (app && !document.body.hasAttribute("data-debug")) void fitHeight(app.offsetHeight);
}

function toggleButton(id: string, initiallyOn: boolean, onChange: (on: boolean) => void) {
  const button = elementById(id);
  let isOn = initiallyOn;
  button.setAttribute("aria-pressed", String(isOn));
  button.onclick = () => {
    isOn = !isOn;
    button.setAttribute("aria-pressed", String(isOn));
    onChange(isOn);
  };
}

async function main() {
  const stats = new SessionStats();
  const plot = startPlot(elementById<HTMLCanvasElement>("plot"));
  buildDialGrid();
  showStatus("Loading", false);

  // The page's own copy of Invert, Recenter and Pause, for the plot and the readouts.
  // To go: the app decides all three (tracker/tracking/ListenerPose.h) and the page should draw its answer.
  const controls = { isInverted: false, isPaused: false, centreYawDegrees: 0, lastYawDegrees: 0 };
  let providerName = "";
  let inferenceMs = 0;

  onEvent("status", (status) => {
    if (controls.isPaused) return;
    providerName = status.detail;
    showStatus(STATUS_TEXT[status.phase](status.detail), status.phase === "tracking");
  });

  onEvent("trackingFrame", (frame) => {
    if (controls.isPaused) return;

    // Timings come with frames the models ran on (a still head repeats the last reading).
    if (frame.detectMs !== undefined && frame.poseMs !== undefined) {
      inferenceMs = frame.detectMs + frame.poseMs;
      elementById("source").textContent = `${providerName} · detect ${frame.detectMs.toFixed(1)} ms · pose ${frame.poseMs.toFixed(1)} ms`;
    }

    showHeadBox(frame.found ? frame.headBox : undefined);

    const yawSign = controls.isInverted ? -1 : 1;
    const yawDegrees = frame.found && frame.cameraYawDegrees !== undefined ? yawSign * frame.cameraYawDegrees : undefined;
    stats.countFrame(yawDegrees);
    showStatus(yawDegrees === undefined ? "No head" : "Tracking", yawDegrees !== undefined);
    if (yawDegrees !== undefined) {
      controls.lastYawDegrees = yawDegrees;
      plot.add(wrapDegrees(yawDegrees - controls.centreYawDegrees), frame.confidence ?? 1);
    }
  });

  onEvent("performance", (p) => {
    elementById("perf").textContent =
      `Camera ${p.cameraWidth}x${p.cameraHeight} at ${p.cameraFramesPerSecond.toFixed(0)}/s · CPU ${p.cpuCores.toFixed(2)} cores · ` +
      `models ${p.estimatedPerSecond.toFixed(0)}/s · reused ${p.reusedPerSecond.toFixed(0)}/s · dropped ${p.droppedPerSecond.toFixed(0)}/s`;
  });

  onEvent("listenerPose", (pose) => {
    if (!pose.live || controls.isPaused) return;
    stats.countHeard(pose.yawDegrees);
    turnDialTo(pose.yawDegrees);
  });

  onEvent("cameraPreview", showCameraPicture);
  onEvent("debugView", showDebugView);
  onEvent("about", () => document.body.toggleAttribute("data-about", true));
  elementById("aboutBack").onclick = () => document.body.toggleAttribute("data-about", false);

  const initial = await pageReady();
  elementById("version").textContent = `Version ${initial.version}`;
  showDebugView(initial.debugView);
  // Opening or closing the camera section changes the page's height.
  const app = document.querySelector(".app");
  if (app) new ResizeObserver(fitWindowToPage).observe(app);
  setUpCameraSection();

  controls.isInverted = initial.inverted;
  toggleButton("invert", initial.inverted, (on) => {
    controls.isInverted = on;
    void setInverted(on);
  });
  toggleButton("pause", false, (on) => {
    controls.isPaused = on;
    void setPaused(on);
  });
  elementById("recenter").onclick = () => {
    controls.centreYawDegrees = controls.lastYawDegrees;
    void recenter();
  };

  setInterval(() => {
    const frames = stats.takeFramesPerSecond();
    elementById("stats").textContent = frames
      ? `${frames} fps · ${inferenceMs.toFixed(1)} ms · jitter ${stats.jitterDegrees().toFixed(2)}° · dropouts ${stats.dropouts}`
      : "";
    elementById("range").textContent =
      `Tracked this session: ${Math.round(stats.turnedDegrees.min)}° … ${Math.round(stats.turnedDegrees.max)}°`;
  }, 1000);
}

main().catch((error) => showStatus(`Error: ${error instanceof Error ? error.message : String(error)}`, false));
