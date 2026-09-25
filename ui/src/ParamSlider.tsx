import { useRef, useState } from "react";
import { beginGesture, type Gesture, type Param } from "./juce";
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
    setDragging(undefined);
  };

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
