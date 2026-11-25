import { useRef } from "react";
import { useJuceEvent } from "./juce";
import { levelOf, type Levels } from "./level";
import styles from "./App.module.css";

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
