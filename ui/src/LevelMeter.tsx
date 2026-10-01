import { useRef } from "react";
import { useJuceEvent } from "./juce";
import styles from "./App.module.css";

// Input level of one player, 0..1 over a 60 dB range. `levels` arrives from C++ at 30 Hz
// (dB by player id, falling ~300 ms after each peak).
export type Levels = Record<string, number>;
export const levelOf = (levels: Levels, id: number) => Math.max(0, 1 + (levels[id] ?? -60) / 60);

// Writes the DOM directly instead of re-rendering React 30 times a second.
export default function LevelMeter({ id, className }: { id: number; className?: string }) {
  const fill = useRef<HTMLSpanElement>(null);

  useJuceEvent<Levels>("levels", (levels) => {
    fill.current?.style.setProperty("--level", String(levelOf(levels, id)));
  });

  return (
    <span className={`${styles.meterTrack} ${className ?? ""}`} aria-hidden>
      <span ref={fill} className={styles.meterFill} />
    </span>
  );
}
