import { useState } from "react";

// A host may give the page no storage (private web views): a preference is then just not kept.
const stored = (key: string) => {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
};
const store = (key: string, value: string) => {
  try {
    localStorage.setItem(key, value);
  } catch {
    // not kept
  }
};

// A view preference of this machine (map view, names on the map), kept in localStorage like
// the load display. Not part of the project: it changes nothing anyone hears.
export function usePref<T extends string>(key: string, initial: T, allowed: readonly T[]) {
  const [value, setValue] = useState<T>(() => {
    const saved = stored(key) as T | null;
    return saved !== null && allowed.includes(saved) ? saved : initial;
  });
  const set = (next: T) => {
    store(key, next);
    setValue(next);
  };
  return [value, set] as const;
}

// The same for a number (a size in pixels): anything stored outside [min, max] is the initial.
export function useNumberPref(key: string, initial: number, min: number, max: number) {
  const [value, setValue] = useState(() => {
    const saved = Number(stored(key) ?? NaN);
    return saved >= min && saved <= max ? saved : initial;
  });
  const set = (next: number) => {
    store(key, String(next));
    setValue(next);
  };
  return [value, set] as const;
}
