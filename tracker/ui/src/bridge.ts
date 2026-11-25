// Everything the page and the app say to each other. The app's side is
// tracker/app/TrackerPage.cpp: same names, same shapes - change the two together.
import { getNativeFunction } from "@juce-framework/webview";

export type InitialState = { inverted: boolean; debugView: boolean; version: string };

export type TrackerStatus = {
  phase: "loadingModels" | "startingCamera" | "compilingModels" | "tracking" | "failed";
  detail: string; // failed: the reason; compilingModels, tracking: what runs the models ("CoreML", "CPU")
};

// One camera frame the app got to. Angles as the camera sees the head (before Invert and Recenter).
export type TrackingFrame = {
  found: boolean;
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

// What Composer hears.
export type ListenerPose = { yawDegrees: number; live: boolean };

type Events = {
  status: TrackerStatus;
  trackingFrame: TrackingFrame;
  performance: Performance;
  cameraPreview: string; // a JPEG data URL
  listenerPose: ListenerPose;
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
