// Dev only: a fake backend so the UI can be viewed in a plain browser (npm run dev).
// Inside the plugin, JUCE injects window.__JUCE__ before any script runs, so this stays inert.
// Must be imported before anything that imports @juce-framework/webview.

import { GestureLedger } from "./gestureLedger";

type Invoke = { name: string; params: unknown[]; resultId: number };
type Edit = { phase: string; target: number; param: string; value: number };

if (import.meta.env.DEV && !window.__JUCE__) {
  const names = ["Violin I", "Violin II", "Viola", "Cello", "Flute", "Horn"];
  const colours = ["#d1495b", "#edae49", "#00798c", "#30638e", "#66a182", ""];
  const scene = {
    scene: "A",
    scenes: ["A", "B"],
    headTracking: { live: location.search.includes("binaural") && !location.search.includes("notlive"), canStart: true },
    pro: {
      proDistanceLaw: 6.0206 / 3,
    } as Record<string, number>,
    outputMode: (location.search.includes("binaural") ? "binaural" : "stereo") as "stereo" | "binaural",
    alone: false,
    // ?elsewhere: another track's window is open (the "open here anyway" screen).
    openElsewhere: location.search.includes("elsewhere") ? "Violin I" : undefined,
    room: 20,
    listener: { x: 0, y: -4 },
    players: names.map((name, i) => ({ id: i, name, colour: colours[i], self: i === 2, x: (i - 2.5) * 2.4, y: 3 + (i % 2) * 2 })),
  };

  // Rough stand-in for Source/dsp/Geometry.h; only so the panel shows plausible numbers.
  const snapshot = () => {
    const half = scene.room / 2;
    const clamp = (v: number) => Math.min(half, Math.max(-half, v));
    const l = { x: clamp(scene.listener.x), y: clamp(scene.listener.y) };
    return {
      ...scene,
      listener: l,
      players: scene.players.map((p) => {
        const x = clamp(p.x);
        const y = clamp(p.y);
        const distance = Math.hypot(x - l.x, y - l.y);
        const azimuth = (Math.atan2(x - l.x, y - l.y) * 180) / Math.PI;
        const folded = Math.abs(azimuth) > 90 ? Math.sign(azimuth) * (180 - Math.abs(azimuth)) : azimuth;
        const derived = {
          distance,
          azimuth,
          pan: folded / 90,
          delayMs: (distance / 343) * 1000,
          levelDb: distance <= 1 ? 0 : (-20 / 3) * Math.log10(distance),
        };
        return { ...p, x, y, derived };
      }),
    };
  };

  const emit = (id: string, payload: unknown) =>
    (window.__JUCE__.backend as unknown as { listeners: { emitEvent(id: string, p: unknown): void } }).listeners.emitEvent(id, payload);
  // The plugin sends the stage from a timer, after the call that changed it has returned.
  const emitStageSoon = () => setTimeout(() => emit("stage", snapshot()), 33);
  const gestures = new GestureLedger();

  const handlers: Record<string, (...args: never[]) => unknown> = {
    getStage: () => snapshot(),
    getBuildInfo: () => ({ version: "0.1.0", format: "Browser", host: "Mock", credits: "Binaural HRTF: SADIE II" }),
    edit: (e: Edit) => {
      gestures.record(e.phase, e.target, e.param);
      if (e.phase !== "change") return;
      const p = scene.players.find((pl) => pl.id === e.target);
      if (e.param === "roomSize") scene.room = e.value;
      if (e.param.startsWith("pro")) scene.pro = { ...scene.pro, [e.param]: e.value };
      if (e.param === "outputMode") scene.outputMode = e.value > 0.5 ? "binaural" : "stereo";
      if (e.param === "listenerX") scene.listener.x = e.value;
      if (e.param === "listenerY") scene.listener.y = e.value;
      if (p && e.param === "playerX") p.x = e.value;
      if (p && e.param === "playerY") p.y = e.value;
      emitStageSoon();
    },
    startTracker: () => true,
    setScene: (name: string) => {
      scene.scene = name;
      if (!scene.scenes.includes(name)) scene.scenes = [...scene.scenes, name].sort();
      emitStageSoon();
    },
    setName: (id: number, name: string) => {
      const p = scene.players.find((pl) => pl.id === id);
      if (p) p.name = name || names[id];
      emitStageSoon();
    },
  };

  (window as unknown as { __JUCE__: object }).__JUCE__ = {
    postMessage(message: string) {
      const { eventId, payload } = JSON.parse(message) as { eventId: string; payload: Invoke };
      if (eventId !== "__juce__invoke") return;
      const result = handlers[payload.name]?.(...(payload.params as never[]));
      setTimeout(() => emit("__juce__complete", { promiseId: payload.resultId, result }));
    },
  };

  // ?pro opens the Pro section (for screenshots; nothing to click in headless Chrome).
  if (location.search.includes("pro"))
    new MutationObserver((_, observer) => {
      const details = document.querySelector("details");
      if (details) {
        details.open = true;
        observer.disconnect();
      }
    }).observe(document, { childList: true, subtree: true });

  // A little fake input level per player (one stays silent).
  setInterval(() => {
    const t = performance.now();
    const levels: Record<string, number> = {};
    for (const p of scene.players) levels[p.id] = p.id === 4 ? -60 : -24 + Math.sin(t / (500 + p.id * 170)) * 22;
    emit("levels", levels);
    emit("headYaw", location.search.includes("binaural") ? Number(new URLSearchParams(location.search).get("yaw") ?? 25) : 0);
  }, 33);
}
