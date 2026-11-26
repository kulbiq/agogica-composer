import { useState } from "react";

// A view preference of this machine (map view, names on the map), kept in localStorage like
// the load display. Not part of the project: it changes nothing anyone hears.
export function usePref<T extends string>(key: string, initial: T, allowed: readonly T[]) {
  const [value, setValue] = useState<T>(() => {
    const stored = localStorage.getItem(key) as T | null;
    return stored !== null && allowed.includes(stored) ? stored : initial;
  });
  const set = (next: T) => {
    localStorage.setItem(key, next);
    setValue(next);
  };
  return [value, set] as const;
}
