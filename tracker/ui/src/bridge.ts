// Everything the page and the app say to each other. The app's side is
// tracker/app/TrackerPage.cpp: same names, same shapes - change the two together.
import { getNativeFunction } from "@juce-framework/webview";

export type InitialState = { inverted: boolean; debugView: boolean; version: string };

export type TrackerStatus = {
  phase: "loadingModels" | "waitingForCamera" | "compilingModels" | "tracking" | "failed";
  // failed: the reason; compilingModels, tracking: what runs the models ("CoreML", "CPU");
  // waitingForCamera: the camera's name, empty when the computer has none.
  detail: string;
};

// One camera frame the app got to (or, with `found` false and no camera angles, the camera going silent).
export type TrackingFrame = {
  found: boolean; // a head is in the frame
  live: boolean; // Composer hears it: found, and not paused
  listenerYawDegrees: number; // what Composer hears (Invert and Recenter applied); 0 while not live
  // As the camera sees the head, before Invert and Recenter:
  reusedStillHead: boolean; // the head hadn't moved: the last reading again, the models didn't run
  changeSinceLastEstimate: number;
  warning?: string; // the models failed on this frame
  cameraYawDegrees?: number;
  cameraPitchDegrees?: number;
  cameraRollDegrees?: number;
  confidence?: number; // 0..1
  headBox?: [x: number, y: number, width: number, height: number]; // in the frame, 0..1
  detectMs?: number; // only when the models ran
  poseMs?: number;
};

export type Performance = {
  cameraWidth: number;
  cameraHeight: number;
  cameraFramesPerSecond: number;
  cpuCores: number; // the whole app, 1 = one core busy
  estimatedPerSecond: number;
  reusedPerSecond: number;
  droppedPerSecond: number;
};

type Events = {
  status: TrackerStatus;
  trackingFrame: TrackingFrame;
  performance: Performance;
  cameraPreview: string; // a JPEG data URL
  debugView: boolean;
  about: undefined;
};

export function onEvent<Name extends keyof Events>(name: Name, handler: (payload: Events[Name]) => void) {
  window.__JUCE__.backend.addEventListener(name, (payload) => handler(payload as Events[Name]));
}

const nativeFunction = <Arguments extends unknown[], Result = void>(name: string) =>
  getNativeFunction(name) as (...args: Arguments) => Promise<Result>;

// Call once the event handlers are in place: the app starts sending events after it.
export const pageReady = nativeFunction<[], InitialState>("pageReady");
export const setInverted = nativeFunction<[on: boolean]>("setInverted");
export const setPaused = nativeFunction<[on: boolean]>("setPaused");
export const recenter = nativeFunction<[]>("recenter");
export const setCameraPreviewEnabled = nativeFunction<[on: boolean]>("setCameraPreviewEnabled");
// The compact view: the app makes the window as tall as the page.
export const fitHeight = nativeFunction<[heightPixels: number]>("fitHeight");
