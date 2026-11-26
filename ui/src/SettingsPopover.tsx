import { useEffect, useRef, useState, type MouseEvent } from "react";
import styles from "./App.module.css";
import { getBuildInfo, setParams, useEvent, type BuildInfo, type Load, type Stage } from "./bridge";
import ParamSlider from "./ParamSlider";
import type { MapNames } from "./stagePainter";

// The defaults of Source/Parameters.h: proDistanceLaw 0..20·log10(2) dB per doubling (2.5);
// proDelayFrom / proDelayTo, the physical delay stretched 1..4 times (1 and 3, dsp/DelayStretch.h).
const lawMax = 6.0206;
const defaults = { proDistanceLaw: 2.5, proDelayFrom: 1, proDelayTo: 3 };
const stretchMax = 4;
const formatStretch = (v: number) => `${v.toFixed(2)}×`;
const minus = "−";
const formatLaw = (v: number) => `${v > 0.05 ? minus : ""}${Math.abs(v).toFixed(1)} dB / 2×`;

const percent = (v: number) => `${(v * 100).toFixed(v < 0.1 ? 1 : 0)}%`;
const loadKey = "showLoad";

type Props = { stage: Stage; selfId: number; names: MapNames; onNames: (names: MapNames) => void; onClose: () => void };

// Settings for the whole plugin (every track, every scene), plus what used to be the footer.
export default function SettingsPopover({ stage, selfId, names, onNames, onClose }: Props) {
  const root = useRef<HTMLDivElement>(null);
  const [info, setInfo] = useState<BuildInfo>();
  const [load, setLoad] = useState<Load>();
  // For us only: always in Debug builds; in Release, Alt+click the version to toggle.
  const [showLoad, setShowLoad] = useState(() => localStorage.getItem(loadKey) === "1");
  const law = stage.pro.proDistanceLaw ?? defaults.proDistanceLaw;
  const stretchFrom = stage.pro.proDelayFrom ?? defaults.proDelayFrom;
  const stretchTo = stage.pro.proDelayTo ?? defaults.proDelayTo;
  const atDefaults =
    Math.abs(law - defaults.proDistanceLaw) < 1e-3 &&
    Math.abs(stretchFrom - defaults.proDelayFrom) < 1e-3 &&
    Math.abs(stretchTo - defaults.proDelayTo) < 1e-3 &&
    names === "selected";

  useEffect(() => {
    void getBuildInfo().then(setInfo);
  }, []);
  useEvent("load", setLoad);

  // Closes on Escape and on a press anywhere outside (the settings button toggles by itself).
  useEffect(() => {
    const onPointerDown = (e: PointerEvent) => {
      const target = e.target as Node;
      if (!root.current?.contains(target) && !root.current?.parentElement?.contains(target)) onClose();
    };
    const onKeyDown = (e: KeyboardEvent) => e.key === "Escape" && onClose();
    document.addEventListener("pointerdown", onPointerDown);
    document.addEventListener("keydown", onKeyDown);
    return () => {
      document.removeEventListener("pointerdown", onPointerDown);
      document.removeEventListener("keydown", onKeyDown);
    };
  }, [onClose]);

  const toggleLoad = (e: MouseEvent) => {
    if (!e.altKey) return;
    localStorage.setItem(loadKey, showLoad ? "0" : "1");
    setShowLoad(!showLoad);
  };

  return (
    <div ref={root} className={styles.popover} role="dialog" aria-label="Settings">
      <div className={styles.popoverHead}>
        <span className={`${styles.label} ${styles.strong}`}>Settings</span>
        <span className={styles.note}>Whole plugin, every scene</span>
      </div>

      <div className={styles.popoverSection}>
        <ParamSlider
          param="proDistanceLaw"
          label="Distance level"
          value={law}
          min={0}
          max={lawMax}
          format={(v) => formatLaw(v)}
          selfId={selfId}
        />
        <p className={styles.note}>How much quieter a player gets each time its distance doubles.</p>
      </div>

      <div className={styles.popoverSection}>
        <ParamSlider param="proDelayFrom" label="Delay stretch from" value={stretchFrom} min={1} max={stretchMax} step={0.01} format={formatStretch} selfId={selfId} />
        <ParamSlider param="proDelayTo" label="Delay stretch to" value={stretchTo} min={1} max={stretchMax} step={0.01} format={formatStretch} selfId={selfId} />
        <p className={styles.note}>Far players arrive later than physics says: nearby places stretch alike, each player anew after a pause.</p>
      </div>

      <div className={`${styles.popoverSection} ${styles.row}`}>
        <span className={styles.label}>Names on map</span>
        <div className={styles.segments} role="radiogroup" aria-label="Names on map">
          {(["selected", "all"] as const).map((n) => (
            <button key={n} className={`${styles.chip} ${styles.segment}`} role="radio" aria-checked={names === n} onClick={() => onNames(n)}>
              {n === "selected" ? "Selected" : "All"}
            </button>
          ))}
        </div>
      </div>

      <div className={styles.popoverSection}>
        <button
          className={styles.ghost}
          disabled={atDefaults}
          title="Distance level −2.5 dB, delay stretch 1× to 3×, names of the selected players only"
          onClick={() => {
            setParams(selfId, defaults);
            onNames("selected");
          }}
        >
          Reset all settings
        </button>
      </div>

      <div className={styles.popoverFoot}>
        <span className={styles.label} onClick={toggleLoad}>
          {info ? `v${info.version} · ${info.format} · ${info.host}` : " "}
        </span>
        {info?.credits && <span className={styles.note}>{info.credits}</span>}
        {(info?.debug || showLoad) && load && (
          <span className={styles.label} title="processBlock's share of one core: this track / all Composer tracks (peak = the worst block)">
            DSP {percent(load.self.average)} · peak {percent(load.self.peak)} · {load.all.tracks}{" "}
            {load.all.tracks === 1 ? "track" : "tracks"} {percent(load.all.average)}
            {load.all.overruns > 0 && ` · over ${load.all.overruns}`}
          </span>
        )}
      </div>
    </div>
  );
}
