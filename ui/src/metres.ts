// Plain decimals as people type them: "1.5", "1,5" (the decimal comma), "-2" or the
// typographic minus. Not hex, not exponents.
export function typedMetres(text: string): number | undefined {
  const plain = text.trim().replace("\u2212", "-").replace(",", ".");
  return /^[-+]?(\d+\.?\d*|\.\d+)$/.test(plain) ? Number(plain) : undefined;
}

// What a position field commits when the user leaves it: the typed text as metres inside the
// room, on the 1 cm grid. Nothing when the text isn't a number, or is still what the field
// showed: the shown number is the place inside the room, and the track may remember one
// outside it for when the room grows again.
export function committedMetres(draft: string, room: number, shown?: string): number | undefined {
  const typed = draft === shown ? undefined : typedMetres(draft);
  if (typed === undefined) return undefined;
  const half = room / 2;
  return Math.round(Math.min(half, Math.max(-half, typed)) * 100) / 100;
}
