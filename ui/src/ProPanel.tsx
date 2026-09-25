import { setParams, type Param } from "./juce";
import ParamSlider from "./ParamSlider";
import styles from "./App.module.css";

// Mirrors the Pro parameters in Source/PluginProcessor.cpp (ranges and neutral defaults).
const offDb = -24;
const minus = "−";
const signed = (v: number, digits = 1) => `${v < 0 ? minus : v > 0 ? "+" : ""}${Math.abs(v).toFixed(digits)}`;
const level = (v: number) => (v <= offDb + 0.05 ? "Off" : `${signed(v)} dB`);
const percent = (v: number) => `${Math.round(v * 100)} %`;
const times = (v: number) => `×${v.toFixed(2)}`;

type Control = { param: Param; label: string; min: number; max: number; step?: number; neutral: number; format: (v: number) => string };

const controls: Control[] = [
  { param: "proReflections", label: "Reflections", min: offDb, max: 6, step: 0.1, neutral: 0, format: level },
  { param: "proDiffusion", label: "Diffusion", min: 0, max: 1, step: 0.01, neutral: 1, format: percent },
  { param: "proFloor", label: "Floor reflection", min: 0, max: 1, step: 0.01, neutral: 1, format: percent },
  { param: "proBrightness", label: "Brightness", min: -12, max: 12, step: 0.1, neutral: 0, format: (v) => `${signed(v)} st` },
  { param: "proReverb", label: "Reverb", min: offDb, max: 6, step: 0.1, neutral: 0, format: level },
  { param: "proDecay", label: "Decay", min: 0.5, max: 2, neutral: 1, format: times },
  { param: "proPreDelay", label: "Pre-delay", min: 0.5, max: 2, neutral: 1, format: times },
  // Default = 20·log10(2)/3, the built-in law (-2 dB per doubling).
  { param: "proDistanceLaw", label: "Level per 2× distance", min: 0, max: 6.0206, neutral: 6.0206 / 3, format: (v) => level(-v) },
  { param: "proAir", label: "Air absorption", min: 0, max: 2, step: 0.01, neutral: 1, format: percent },
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
        <p className={styles.note}>Project-wide adjustments on top of the hall, for every scene.</p>
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
