import { useEffect, useRef, useState } from "react";
import { getNativeFunction } from "@juce-framework/webview";

export type Point = { x: number; y: number };

export type Player = Point & {
  id: number;
  name: string;
  colour: string; // host track colour "#rrggbb", or ""
  self: boolean;
  derived: { distance: number; azimuth: number; pan: number; delayMs: number; levelDb: number; airDb: number };
};

// Mirrors stage::snapshot in Source/bridge/Stage.cpp.
export type Stage = {
  scene: string; // active scene, "A".."H"
  scenes: string[]; // scenes that hold a layout (the active one included)
  outputMode: "stereo" | "binaural";
  hall: number; // index into `halls`, stored per scene
  halls: string[];
  alone: boolean;
  room: number;
  listener: Point;
  players: Player[];
};

export type Param = "roomSize" | "listenerX" | "listenerY" | "playerX" | "playerY" | "outputMode" | "hall";

const getStage = getNativeFunction("getStage") as () => Promise<Stage>;
const editNative = getNativeFunction("edit");
export const setName = getNativeFunction("setName") as (id: number, name: string) => Promise<void>;
export const setScene = getNativeFunction("setScene") as (scene: string) => Promise<void>;

// Listens to an event C++ sends with emitEventIfBrowserIsVisible.
export function useJuceEvent<T>(id: string, fn: (payload: T) => void) {
  const latest = useRef(fn);
  latest.current = fn;

  useEffect(() => {
    const token = window.__JUCE__.backend.addEventListener(id, (p) => latest.current(p as T));
    return () => window.__JUCE__.backend.removeEventListener(token);
  }, [id]);
}

// The current stage; C++ re-sends it whenever anything changes (drags, automation, other tracks).
export function useStage() {
  const [stage, setStage] = useState<Stage>();

  useEffect(() => {
    void getStage().then((s) => setStage((current) => current ?? s));
  }, []);

  useJuceEvent<Stage>("stage", setStage);
  return stage;
}

// One user gesture on `target`'s parameters: begin, any number of changes (sent at most
// once per frame), end. Hosts record automation only for writes inside a gesture.
export function beginGesture(target: number, params: Param[]) {
  for (const param of params) void editNative({ phase: "begin", target, param });

  let pending: Partial<Record<Param, number>> = {};
  let frame = 0;

  const flush = () => {
    cancelAnimationFrame(frame);
    frame = 0;
    for (const [param, value] of Object.entries(pending))
      void editNative({ phase: "change", target, param, value });
    pending = {};
  };

  return {
    change(values: Partial<Record<Param, number>>) {
      pending = { ...pending, ...values };
      if (!frame) frame = requestAnimationFrame(flush);
    },
    end() {
      flush();
      for (const param of params) void editNative({ phase: "end", target, param });
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
