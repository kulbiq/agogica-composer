// Input level of one player, 0..1 over a 60 dB range. `levels` arrives from C++ at 30 Hz
// (dB by player id, falling ~300 ms after each peak).
export type Levels = Record<string, number>;
export const levelOf = (levels: Levels, id: number) => Math.min(1, Math.max(0, 1 + (levels[id] ?? -60) / 60));
