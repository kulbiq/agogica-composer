// Dev only: a fake backend so the UI can be viewed in a plain browser (npm run dev).
// Inside the plugin, JUCE injects window.__JUCE__ before any script runs, so this stays inert.
// Must be imported before anything that imports @juce-framework/webview.

import { GestureLedger } from "./gestureLedger";

type Invoke = { name: string; params: unknown[]; resultId: number };
type Edit = { phase: string; target: number; param: string; value: number };

if (import.meta.env.DEV && !window.__JUCE__) {
  // An orchestra in front of the listener (the design canvas's stage).
  const seats: [string, string, number, number][] = [
    ["Violin I", "#d1495b", -5, 2.5],
    ["Violin II", "#edae49", -3, 4.5],
    ["Viola", "#00798c", -1.2, 3],
    ["Cello", "#30638e", 1.5, 3.2],
    ["Bass", "#30638e", 4.5, 3.5],
    ["Flute", "#66a182", -1.5, 6],
    ["Clarinet", "#66a182", 1.5, 6],
    ["Horn", "#edae49", -4.5, 7],
    ["Trumpet", "#edae49", 4.5, 7],
    ["Timpani", "", 0, 8.5],
    ["Harp", "#d1495b", -7, 4.5],
    ["Piano", "", 7, 5],
  ];
  const names = seats.map(([name]) => name);
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
    listener: { x: 0, y: 0 },
    players: seats.map(([name, colour, x, y], i) => ({ id: i, name, colour, self: i === 2, x, y })),
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

  // ?azimuth and ?allnames set the map preferences (before the app reads them).
  if (location.search.includes("azimuth")) localStorage.setItem("mapView", "azimuth");
  if (location.search.includes("allnames")) localStorage.setItem("mapNames", "all");

  // ?settings opens the settings popover (for screenshots; nothing to click in headless Chrome).
  if (location.search.includes("settings"))
    new MutationObserver((_, observer) => {
      const button = document.querySelector<HTMLButtonElement>('button[aria-label="Settings"]');
      if (button) {
        button.click();
        observer.disconnect();
      }
    }).observe(document, { childList: true, subtree: true });

  // A little fake input level per player (the timpani stays silent).
  setInterval(() => {
    const t = performance.now();
    const levels: Record<string, number> = {};
    for (const p of scene.players) levels[p.id] = p.name === "Timpani" ? -60 : -24 + Math.sin(t / (500 + p.id * 170)) * 22;
    emit("levels", levels);
    emit("headYaw", location.search.includes("binaural") ? Number(new URLSearchParams(location.search).get("yaw") ?? 25) : 0);
  }, 33);
}
