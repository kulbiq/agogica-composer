// Dev only: a fake backend so the UI can be viewed in a plain browser (npm run dev).
// Inside the plugin, JUCE injects window.__JUCE__ before any script runs, so this stays inert.
// Must be imported before anything that imports @juce-framework/webview.

import { emptyAudience, encodeAudience, setSeat } from "./audience";
import { GestureLedger } from "./gestureLedger";

type Invoke = { name: string; params: unknown[]; resultId: number };
type Edit = { phase: string; target: number; param: string; value: number };

if (import.meta.env.DEV && !window.__JUCE__) {
  // An orchestra in front of the listener (the design canvas's stage): one track per player,
  // except the horns and the piano (two tracks each, see players.ts).
  const seats: [string, string, number, number, string?][] = [
    ["Violin I", "#d1495b", -5, 2.5],
    ["Violin II", "#edae49", -3, 4.5],
    ["Viola", "#00798c", -1.2, 3],
    ["Cello", "#30638e", 1.5, 3.2],
    ["Bass", "#30638e", 4.5, 3.5],
    ["Flute", "#66a182", -1.5, 6],
    ["Clarinet", "#66a182", 1.5, 6],
    ["Horn 1", "#edae49", -4.5, 7, "Horns"],
    ["Horn 2", "#edae49", -4.5, 7, "Horns"],
    ["Trumpet", "#edae49", 4.5, 7],
    ["Timpani", "", 0, 8.5],
    ["Harp", "#d1495b", -7, 4.5],
    ["Piano RH", "", 7, 5, "Piano"],
    ["Piano LH", "", 7, 5, "Piano"],
  ];
  // ?players=64: that many tracks instead (a big score, for measuring the editor's cost), in
  // rows of 8 in front of the listener, a pair sharing a player every 8 tracks.
  const many = Number(new URLSearchParams(location.search).get("players") ?? 0);
  if (many > 0) {
    const palette = ["#d1495b", "#edae49", "#00798c", "#30638e", "#66a182", ""];
    seats.length = 0;
    for (let i = 0; i < many; i++) {
      const pair = i % 8 === 1 ? `Desk ${i}` : i % 8 === 2 ? `Desk ${i - 1}` : undefined;
      seats.push([`Track ${i + 1}`, palette[i % palette.length], ((i % 8) - 3.5) * 2, 2 + Math.floor(i / 8) * 1.5, pair]);
    }
  }
  const names = seats.map(([name]) => name);
  const scene = {
    scene: "A",
    scenes: ["A", "B"],
    headTracking: { live: location.search.includes("binaural") && !location.search.includes("notlive"), canStart: true, on: true },
    pro: {
      proDistanceLaw: 2.5,
      proDelayFrom: 1,
      proDelayTo: 3,
    } as Record<string, number>,
    outputMode: (location.search.includes("binaural") ? "binaural" : "stereo") as "stereo" | "binaural",
    alone: false,
    // ?elsewhere: another track's window is open (the "open here anyway" screen).
    openElsewhere: location.search.includes("elsewhere") ? "Violin I" : undefined,
    room: Number(new URLSearchParams(location.search).get("room") ?? 20), // ?room=50: another size
    listener: { x: 0, y: 0, yaw: 0 },
    audience: sampleAudience(),
    // `own`: the track's name; `playerName`: the player's, when it has one (see setName).
    players: seats.map(([own, colour, x, y, player], i) => ({
      id: i, own, playerName: player ?? "", colour, icon: "", player: player?.toLowerCase() ?? "", self: i === 2, x, y,
    })),
  };

  // Rough stand-in for Source/dsp/Geometry.h; only so the panel shows plausible numbers.
  const snapshot = () => {
    const half = scene.room / 2;
    const clamp = (v: number) => Math.min(half, Math.max(-half, v));
    const l = { x: clamp(scene.listener.x), y: clamp(scene.listener.y), yaw: scene.listener.yaw };
    return {
      ...scene,
      listener: l,
      players: scene.players.map((p) => {
        const x = clamp(p.x);
        const y = clamp(p.y);
        return { ...p, name: p.playerName || p.own, track: p.own, x, y };
      }),
    };
  };

  const emit = (id: string, payload: unknown) =>
    (window.__JUCE__.backend as unknown as { listeners: { emitEvent(id: string, p: unknown): void } }).listeners.emitEvent(id, payload);
  // The plugin sends the stage from a timer, after the call that changed it has returned.
  const emitStageSoon = () => setTimeout(() => emit("stage", snapshot()), 33);
  const gestures = new GestureLedger();
  const tracksOf = (id: number) => {
    const key = scene.players.find((p) => p.id === id)?.player;
    return scene.players.filter((p) => p.id === id || (key && p.player === key));
  };
  const dropLoneKeys = () =>
    scene.players.forEach((p) => {
      if (p.player && scene.players.filter((q) => q.player === p.player).length === 1) p.player = "";
    });

  const handlers: Record<string, (...args: never[]) => unknown> = {
    getStage: () => snapshot(),
    getBuildInfo: () => ({ version: "0.1.0", format: "Browser", host: "Mock", credits: "Binaural HRTF: SADIE II" }),
    edit: (e: Edit) => {
      gestures.record(e.phase, e.target, e.param);
      if (e.phase !== "change") return;
      const p = scene.players.find((pl) => pl.id === e.target);
      const tracks = tracksOf(e.target);
      if (e.param === "roomSize") scene.room = e.value;
      if (e.param.startsWith("pro")) scene.pro = { ...scene.pro, [e.param]: e.value };
      if (e.param === "outputMode") scene.outputMode = e.value > 0.5 ? "binaural" : "stereo";
      if (e.param === "listenerX") scene.listener.x = e.value;
      if (e.param === "listenerY") scene.listener.y = e.value;
      if (e.param === "listenerYaw") scene.listener.yaw = e.value;
      if (p && e.param === "playerX") tracks.forEach((t) => (t.x = e.value));
      if (p && e.param === "playerY") tracks.forEach((t) => (t.y = e.value));
      emitStageSoon();
    },
    startTracker: () => true,
    setAudience: (seats: string) => {
      scene.audience = seats;
      emitStageSoon();
    },
    setHeadTracking: (on: boolean) => {
      scene.headTracking = { ...scene.headTracking, on };
      emitStageSoon();
    },
    setScene: (name: string) => {
      scene.scene = name;
      if (!scene.scenes.includes(name)) scene.scenes = [...scene.scenes, name].sort();
      emitStageSoon();
    },
    // Like stage::setName / setTrackName (Source/bridge/StageEdits.cpp).
    setName: (id: number, name: string) => {
      const p = scene.players.find((pl) => pl.id === id);
      if (p && !p.player) Object.assign(p, { own: name || names[id], playerName: "" });
      else tracksOf(id).forEach((t) => (t.playerName = name));
      emitStageSoon();
    },
    setTrackName: (id: number, name: string) => {
      const p = scene.players.find((pl) => pl.id === id);
      if (p) p.own = name || names[id];
      emitStageSoon();
    },
    setIcon: (id: number, icon: string) => {
      tracksOf(id).forEach((t) => (t.icon = icon));
      emitStageSoon();
    },
    // Like stage::joinPlayer / splitPlayer (Source/bridge/StageEdits.cpp).
    joinPlayer: (trackId: number, intoId: number) => {
      const track = scene.players.find((p) => p.id === trackId);
      const into = scene.players.find((p) => p.id === intoId);
      if (!track || !into || track === into || (into.player && track.player === into.player)) return;
      if (!track.player && track.playerName) track.own = track.playerName;
      into.player ||= `p${intoId}`;
      Object.assign(track, { player: into.player, x: into.x, y: into.y });
      const playerName = into.playerName || into.own;
      tracksOf(intoId).forEach((t) => Object.assign(t, { playerName, icon: into.icon }));
      dropLoneKeys();
      emitStageSoon();
    },
    splitPlayer: (trackId: number) => {
      const track = scene.players.find((p) => p.id === trackId);
      if (!track?.player) return;
      Object.assign(track, { player: "", playerName: "", icon: "" });
      dropLoneKeys();
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

  // ?settings opens the settings popover, ?edit edits this track's player, ?3d walks into the
  // room (for screenshots; nothing to click in headless Chrome).
  const clickOnce = (find: () => HTMLButtonElement | undefined) =>
    new MutationObserver((_, observer) => {
      const button = find();
      if (button) {
        button.click();
        observer.disconnect();
      }
    }).observe(document, { childList: true, subtree: true });
  const buttons = () => [...document.querySelectorAll("button")];
  if (location.search.includes("settings")) clickOnce(() => buttons().find((b) => b.ariaLabel === "Settings"));
  if (location.search.includes("edit")) clickOnce(() => buttons().find((b) => b.textContent === "Edit player"));
  if (location.search.includes("3d")) clickOnce(() => buttons().find((b) => b.textContent === "3D"));

  // A little fake input level per player (the timpani stays silent).
  // ?still: levels that don't move (for comparing screenshots).
  setInterval(() => {
    const t = location.search.includes("still") ? 0 : performance.now();
    const levels: Record<string, number> = {};
    for (const p of scene.players) levels[p.id] = p.own === "Timpani" ? -60 : -24 + Math.sin(t / (500 + p.id * 170)) * 22;
    emit("levels", levels);
    emit("headYaw", location.search.includes("binaural") ? Number(new URLSearchParams(location.search).get("yaw") ?? 25) : 0);
  }, 33);
}

// Rows behind the stage's centre with a middle aisle (?noaudience: nobody; ?audience=600: about
// that many seats, rows filling the back of a big room, as a large project has them).
function sampleAudience() {
  const seats = emptyAudience();
  const many = Number(new URLSearchParams(location.search).get("audience") ?? 0);
  if (many > 0) {
    let n = 0;
    for (let j = -2; j >= -30 && n < many; j--)
      for (let i = 1; i <= 24 && n < many; i++) {
        setSeat(seats, i, j, true);
        setSeat(seats, -i, j, true);
        n += 2;
      }
    return encodeAudience(seats);
  }
  if (!location.search.includes("noaudience"))
    for (let j = -4; j >= -11; j--)
      for (let i = 2; i <= 9; i++) {
        setSeat(seats, i, j, true);
        setSeat(seats, -i, j, true);
      }
  return encodeAudience(seats);
}
