// What a position field commits when the user leaves it: the typed text as metres inside the
// room, on the 1 cm grid, or nothing when the text isn't a number.
export function committedMetres(draft: string, room: number): number | undefined {
  const typed = Number(draft);
  if (draft.trim() === "" || !Number.isFinite(typed)) return undefined;
  const half = room / 2;
  return Math.round(Math.min(half, Math.max(-half, typed)) * 100) / 100;
}
