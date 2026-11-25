import "../../../ui/src/styles/fonts.css";
import "../../../ui/src/styles/tokens.css";
import "./tracker.css";
import { fitHeight, onEvent, pageReady, recenter, setInverted, setPaused, type TrackerStatus, type TrackingFrame } from "./bridge";
import { setUpCameraSection, showCameraPicture, showHeadBox } from "./cameraPreview";
import { buildDialGrid, turnDialTo } from "./dial";
import { elementById } from "./dom";
import { startPlot } from "./plot";
import { SessionStats } from "./sessionStats";

// The app tracks the head, decides what Composer hears and writes it; this page only shows it
// ("trackingFrame" events, one per camera frame) and sends the buttons.

const STATUS_TEXT: Record<TrackerStatus["phase"], (detail: string) => string> = {
  loadingModels: () => "Loading model",
  waitingForCamera: (cameraName) => (cameraName ? "Starting camera" : "No camera"),
  compilingModels: (provider) => `Compiling for ${provider}`,
  tracking: () => "Tracking",
  failed: (reason) => `Error: ${reason}`,
};

// The chip: the app's status until it tracks, then what the latest frame says.
function showStatus(status: TrackerStatus | undefined, frame: TrackingFrame | undefined, isPaused: boolean) {
  const chip = elementById("status");
  const isTracking = status?.phase === "tracking";
  let text = "Loading";
  if (status && !isTracking) text = STATUS_TEXT[status.phase](status.detail);
  else if (isPaused) text = "Paused";
  else if (frame && !frame.found) text = "No head";
  else if (isTracking) text = "Tracking";

  chip.textContent = text;
  chip.dataset.status = isTracking && frame?.live ? "live" : "soon";
  // The longer story on hover: which camera, or why a frame failed.
  chip.title = status?.phase === "waitingForCamera" ? status.detail : (frame?.warning ?? "");
}

function showError(error: unknown) {
  const chip = elementById("status");
  chip.textContent = `Error: ${error instanceof Error ? error.message : String(error)}`;
  chip.dataset.status = "soon";
}

// Debugging (the app's View menu) shows the readouts and the plot; off, the window is fixed and
// the app fits it to the page.
function showDebugView(on: boolean) {
  document.body.toggleAttribute("data-debug", on);
  fitWindowToPage();
}

let fittedHeight = 0;
function fitWindowToPage() {
  const app = document.querySelector<HTMLElement>(".app");
  if (!app || document.body.hasAttribute("data-debug")) {
    fittedHeight = 0; // the debugging view sizes itself: fit again when the compact view returns
    return;
  }
  if (app.offsetHeight !== fittedHeight) void fitHeight((fittedHeight = app.offsetHeight));
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
  const recenterButton = elementById<HTMLButtonElement>("recenter");
  buildDialGrid();

  let status: TrackerStatus | undefined;
  let latestFrame: TrackingFrame | undefined;
  let isPaused = false;
  let inferenceMs = 0;
  const refreshStatus = () => showStatus(status, latestFrame, isPaused);
  refreshStatus();

  onEvent("status", (newStatus) => {
    status = newStatus;
    refreshStatus();
  });

  onEvent("trackingFrame", (frame) => {
    latestFrame = frame;
    stats.count(frame);
    showHeadBox(frame.headBox);
    recenterButton.disabled = !frame.live; // there is nothing to centre on
    refreshStatus();

    if (frame.live) {
      turnDialTo(frame.listenerYawDegrees);
      plot.add(frame.listenerYawDegrees, frame.confidence ?? 1);
    }

    // Timings come with frames the models ran on (a still head repeats the last reading).
    if (frame.detectMs !== undefined && frame.poseMs !== undefined) {
      inferenceMs = frame.detectMs + frame.poseMs;
      elementById("source").textContent =
        `${status?.detail ?? ""} · detect ${frame.detectMs.toFixed(1)} ms · pose ${frame.poseMs.toFixed(1)} ms`;
    }
  });

  onEvent("performance", (p) => {
    elementById("perf").textContent =
      `Camera ${p.cameraWidth}x${p.cameraHeight} at ${p.cameraFramesPerSecond.toFixed(0)}/s · CPU ${p.cpuCores.toFixed(2)} cores · ` +
      `models ${p.estimatedPerSecond.toFixed(0)}/s · reused ${p.reusedPerSecond.toFixed(0)}/s · dropped ${p.droppedPerSecond.toFixed(0)}/s`;
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

  toggleButton("invert", initial.inverted, (on) => void setInverted(on));
  toggleButton("pause", false, (on) => {
    isPaused = on;
    refreshStatus();
    void setPaused(on);
  });
  recenterButton.disabled = true;
  recenterButton.onclick = () => void recenter();

  setInterval(() => {
    const frames = stats.takeFramesPerSecond();
    elementById("stats").textContent = frames
      ? `${frames} fps · ${inferenceMs.toFixed(1)} ms · jitter ${stats.jitterDegrees().toFixed(2)}° · dropouts ${stats.dropouts}`
      : "";
    elementById("range").textContent =
      `Tracked this session: ${Math.round(stats.turnedDegrees.min)}° … ${Math.round(stats.turnedDegrees.max)}°`;
  }, 1000);
}

main().catch(showError);
