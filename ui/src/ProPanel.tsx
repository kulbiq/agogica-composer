import { setParams, type Param } from "./bridge";
import ParamSlider from "./ParamSlider";
import styles from "./App.module.css";

// Mirrors the Pro parameters in Source/PluginProcessor.cpp (ranges and neutral defaults).
const minus = "−";
const signed = (v: number, digits = 1) => `${v < 0 ? minus : v > 0 ? "+" : ""}${Math.abs(v).toFixed(digits)}`;
const level = (v: number) => `${signed(v)} dB`;

type Control = { param: Param; label: string; min: number; max: number; step?: number; neutral: number; format: (v: number) => string };

const controls: Control[] = [
  // Default = 20·log10(2)/3, the built-in law (-2 dB per doubling).
  { param: "proDistanceLaw", label: "Level per 2× distance", min: 0, max: 6.0206, neutral: 6.0206 / 3, format: (v) => level(-v) },
];

export default function ProPanel({ values, selfId }: { values: Record<string, number>; selfId: number }) {
  const changed = controls.some((c) => Math.abs((values[c.param] ?? c.neutral) - c.neutral) > 1e-3);

  return (
    <details className={styles.pro}>
      <summary className={styles.proSummary}>
        <span className={styles.label}>Pro</span>
        {changed && <span className={styles.you}>Adjusted</span>}
      </summary>
      <div className={styles.proBody}>
        <p className={styles.note}>Project-wide adjustments, for every scene.</p>
        {controls.map((c) => (
          <ParamSlider key={c.param} {...c} value={values[c.param] ?? c.neutral} selfId={selfId} />
        ))}
        <button
          className={styles.ghost}
          disabled={!changed}
          onClick={() => {
            for (const c of controls) setParams(selfId, { [c.param]: c.neutral });
          }}
        >
          Reset
        </button>
      </div>
    </details>
  );
}
