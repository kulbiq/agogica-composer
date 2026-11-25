// What a position field commits when the user leaves it: the typed text as metres inside the
// room, on the 1 cm grid. Nothing when the text isn't a number, or is still what the field
// showed: the shown number is the place inside the room, and the track may remember one
// outside it for when the room grows again.
export function committedMetres(draft: string, room: number, shown?: string): number | undefined {
  const typed = Number(draft);
  if (draft === shown || draft.trim() === "" || !Number.isFinite(typed)) return undefined;
  const half = room / 2;
  return Math.round(Math.min(half, Math.max(-half, typed)) * 100) / 100;
}
