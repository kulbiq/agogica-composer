import { useEffect, useReducer, useRef } from "react";
import { getSliderState } from "@juce-framework/webview";

// A JUCE WebSliderRelay; re-renders when C++ sends a new value or range.
export function useSlider(name: string) {
  const state = getSliderState(name);
  const [, rerender] = useReducer((n: number) => n + 1, 0);

  useEffect(() => {
    const value = state.valueChangedEvent.addListener(rerender);
    const props = state.propertiesChangedEvent.addListener(rerender);
    return () => {
      state.valueChangedEvent.removeListener(value);
      state.propertiesChangedEvent.removeListener(props);
    };
  }, [state]);

  return state;
}

// Listens to an event C++ sends with emitEventIfBrowserIsVisible.
export function useJuceEvent<T>(id: string, fn: (payload: T) => void) {
  const latest = useRef(fn);
  latest.current = fn;

  useEffect(() => {
    const token = window.__JUCE__.backend.addEventListener(id, (p) => latest.current(p as T));
    return () => window.__JUCE__.backend.removeEventListener(token);
  }, [id]);
}
