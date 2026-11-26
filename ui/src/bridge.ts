// The page's side of the editor: the only file with the names of its functions and events,
// and their shapes. The editor's side is Source/bridge/PageProtocol.h (names) and the files it
// points to: the two are changed together.
import { useEffect, useRef, useState } from "react";
import { getNativeFunction } from "@juce-framework/webview";
import type { Levels } from "./level";

export type Point = { x: number; y: number };

export type Player = Point & {
  id: number;
  name: string;
  colour: string; // host track colour "#rrggbb", or ""
  icon: string; // a key from icons.tsx, or "" = picked from the name
  self: boolean;
};

// What stage::snapshot (Source/bridge/StageSnapshot.cpp) sends.
export type Stage = {
  scene: string; // active scene, "A".."H"
  scenes: string[]; // scenes that hold a layout (the active one included)
  outputMode: "stereo" | "binaural";
  pro: Record<string, number>; // Pro panel parameter values (project-wide)
  headTracking: { live: boolean; canStart: boolean }; // the yaw comes as its own "headYaw" event (degrees clockwise)
  alone: boolean;
  openElsewhere?: string; // name of another track whose Composer window is open
  room: number;
  listener: Point;
  players: Player[];
};

export type Param = "roomSize" | "listenerX" | "listenerY" | "playerX" | "playerY" | "outputMode"
  | "proDistanceLaw";

export type BuildInfo = { version: string; format: string; host: string; credits?: string; debug?: boolean };

// Development monitoring (Source/dsp/LoadMeter.h): processBlock's share of one core, this
// track and all Composer tracks in this host process.
export type Load = {
  self: { average: number; peak: number; overruns: number };
  all: { average: number; peak: number; overruns: number; tracks: number };
};

// One step of a gesture on one parameter of track `target`.
type Edit = { phase: "begin" | "change" | "end"; target: number; param: Param; value?: number };

// Functions the page calls.
const getStage = getNativeFunction("getStage") as () => Promise<Stage>;
const edit = getNativeFunction("edit") as (edit: Edit) => Promise<void>;
export const setName = getNativeFunction("setName") as (id: number, name: string) => Promise<void>;
export const setIcon = getNativeFunction("setIcon") as (id: number, icon: string) => Promise<void>;
export const setScene = getNativeFunction("setScene") as (scene: string) => Promise<void>;
export const startTracker = getNativeFunction("startTracker") as () => Promise<boolean>;
export const getBuildInfo = getNativeFunction("getBuildInfo") as () => Promise<BuildInfo>;
export const setEditorSize = getNativeFunction("setEditorSize") as (width: number, height: number) => Promise<void>;

// Events the editor sends, and what each carries.
type Events = {
  stage: Stage; // when it changed
  levels: Levels; // 30 times a second
  headYaw: number; // the listener's head, degrees clockwise, when it moved
  load: Load; // about 4 times a second
};

export function useEvent<Name extends keyof Events>(name: Name, fn: (payload: Events[Name]) => void) {
  const latest = useRef(fn);
  latest.current = fn;

  useEffect(() => {
    const token = window.__JUCE__.backend.addEventListener(name, (p) => latest.current(p as Events[Name]));
    return () => window.__JUCE__.backend.removeEventListener(token);
  }, [name]);
}

// The current stage; C++ re-sends it whenever anything changes (drags, automation, other tracks).
export function useStage() {
  const [stage, setStage] = useState<Stage>();

  useEffect(() => {
    void getStage().then((s) => setStage((current) => current ?? s));
  }, []);

  useEvent("stage", setStage);
  return stage;
}

// One user gesture on `target`'s parameters: begin, any number of changes (sent at most
// once per frame), end. Hosts record automation only for writes inside a gesture.
export function beginGesture(target: number, params: Param[]) {
  for (const param of params) void edit({ phase: "begin", target, param });

  let pending: Partial<Record<Param, number>> = {};
  let frame = 0;

  const flush = () => {
    cancelAnimationFrame(frame);
    frame = 0;
    for (const [param, value] of Object.entries(pending) as [Param, number][])
      void edit({ phase: "change", target, param, value });
    pending = {};
  };

  return {
    change(values: Partial<Record<Param, number>>) {
      pending = { ...pending, ...values };
      if (!frame) frame = requestAnimationFrame(flush);
    },
    end() {
      flush();
      for (const param of params) void edit({ phase: "end", target, param });
    },
  };
}

export type Gesture = ReturnType<typeof beginGesture>;

// A whole gesture in one go, for typed-in values.
export function setParams(target: number, values: Partial<Record<Param, number>>) {
  const g = beginGesture(target, Object.keys(values) as Param[]);
  g.change(values);
  g.end();
}
