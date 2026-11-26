import { useEffect, useRef, useState } from "react";
import { beginGesture, type Gesture, type Param } from "./bridge";
import styles from "./App.module.css";

const gestureKeys = new Set(["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "PageUp", "PageDown", "Home", "End"]);

type Props = {
  param: Param;
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  format: (value: number) => string;
  selfId: number; // edits go through this instance; C++ fans shared params out
};

// A parameter slider: every change sits inside a begin/end gesture so hosts record
// automation; while dragging it shows the local value (C++ confirms a frame later).
export default function ParamSlider({ param, label, value, min, max, step, format, selfId }: Props) {
  const [dragging, setDragging] = useState<number>();
  const gesture = useRef<Gesture>(undefined);
  const shown = dragging ?? value;

  const begin = () => {
    gesture.current ??= beginGesture(selfId, [param]);
  };
  const end = () => {
    gesture.current?.end();
    gesture.current = undefined;
  };

  // After the release the local value stays until C++ reports its own (a frame later), so the
  // thumb doesn't jump back for that frame; half a second is the latest it lets go.
  useEffect(() => {
    if (dragging === undefined || gesture.current) return;
    const timer = setTimeout(() => setDragging(undefined), 500);
    return () => clearTimeout(timer);
  });
  useEffect(() => {
    if (!gesture.current) setDragging(undefined);
  }, [value]);

  // Closed in the middle of a change (Escape closes the settings): the host still gets its end.
  useEffect(() => () => gesture.current?.end(), []);

  return (
    <label className={styles.field}>
      <span className={styles.row}>
        <span className={styles.label}>{label}</span>
        <span className={styles.value}>{format(shown)}</span>
      </span>
      <input
        className={styles.range}
        type="range"
        min={min}
        max={max}
        step={step ?? "any"}
        value={shown}
        onPointerDown={begin}
        onPointerUp={end}
        onPointerCancel={end}
        onKeyDown={(e) => gestureKeys.has(e.key) && begin()}
        onKeyUp={(e) => gestureKeys.has(e.key) && end()}
        onBlur={end}
        onChange={(e) => {
          begin();
          const v = Number(e.target.value);
          setDragging(v);
          gesture.current!.change({ [param]: v });
        }}
      />
    </label>
  );
}
