import { useRef, type KeyboardEvent } from "react";
import { useSlider } from "./juce";
import styles from "./App.module.css";

const gestureKeys = new Set(["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "PageUp", "PageDown", "Home", "End"]);

export default function RoomSizeSlider() {
  const slider = useSlider("roomSize");
  const { start, end, interval, label } = slider.properties;
  const value = slider.getScaledValue();
  // Every change sits inside a begin/end gesture so hosts record automation.
  const inGesture = useRef(false);

  const begin = () => {
    if (!inGesture.current) slider.sliderDragStarted();
    inGesture.current = true;
  };
  const end_ = () => {
    if (inGesture.current) slider.sliderDragEnded();
    inGesture.current = false;
  };
  const onKey = (e: KeyboardEvent, down: boolean) => {
    if (gestureKeys.has(e.key)) (down ? begin : end_)();
  };

  return (
    <label className={styles.field}>
      <span className={styles.fieldHead}>
        <span className={styles.label}>Room size</span>
        <span className={styles.value}>
          {value} {label}
        </span>
      </span>
      <input
        className={styles.range}
        type="range"
        min={start}
        max={end}
        step={interval || "any"}
        value={value}
        onPointerDown={begin}
        onPointerUp={end_}
        onPointerCancel={end_}
        onKeyDown={(e) => onKey(e, true)}
        onKeyUp={(e) => onKey(e, false)}
        onBlur={end_}
        onChange={(e) => slider.setNormalisedValue((Number(e.target.value) - start) / (end - start))}
      />
    </label>
  );
}
