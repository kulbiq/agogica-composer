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

// The same for a number (a size in pixels): anything stored outside [min, max] is the initial.
export function useNumberPref(key: string, initial: number, min: number, max: number) {
  const [value, setValue] = useState(() => {
    const stored = Number(localStorage.getItem(key) ?? NaN);
    return stored >= min && stored <= max ? stored : initial;
  });
  const set = (next: number) => {
    localStorage.setItem(key, String(next));
    setValue(next);
  };
  return [value, set] as const;
}
