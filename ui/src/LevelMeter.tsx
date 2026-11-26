import { useRef } from "react";
import { useEvent } from "./bridge";
import { seatLevel } from "./players";
import styles from "./App.module.css";

// The loudest of these tracks. Writes the DOM directly instead of re-rendering React 30
// times a second.
export default function LevelMeter({ ids, className }: { ids: number[]; className?: string }) {
  const fill = useRef<HTMLSpanElement>(null);

  useEvent("levels", (levels) => {
    fill.current?.style.setProperty("--level", String(seatLevel(levels, ids)));
  });

  return (
    <span className={`${styles.meterTrack} ${className ?? ""}`} aria-hidden>
      <span ref={fill} className={styles.meterFill} />
    </span>
  );
}
