import { useEffect, useRef, useState, type MouseEvent } from "react";
import styles from "./App.module.css";
import { getBuildInfo, setParams, setTelemetry, useEvent, type BuildInfo, type Load, type Stage } from "./bridge";
import ParamSlider from "./ParamSlider";
import { usePref } from "./prefs";
import type { MapNames } from "./stagePainter";
import { neverShared, whatIsShared } from "./TelemetryConsent";

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
  const [showLoad, setShowLoad] = usePref(loadKey, "0", ["0", "1"] as const);
  const law = stage.pro.proDistanceLaw ?? defaults.proDistanceLaw;
  const stretchFrom = stage.pro.proDelayFrom ?? defaults.proDelayFrom;
  const stretchTo = stage.pro.proDelayTo ?? defaults.proDelayTo;
  const atDefaults =
    Math.abs(law - defaults.proDistanceLaw) < 1e-3 &&
    Math.abs(stretchFrom - defaults.proDelayFrom) < 1e-3 &&
    Math.abs(stretchTo - defaults.proDelayTo) < 1e-3 &&
    names === "selected";

  const sharing = stage.telemetry === "on";
  useEffect(() => {
    void getBuildInfo().then(setInfo);
  }, [sharing]); // the id it shows comes and goes with the answer
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
    setShowLoad(showLoad === "1" ? "0" : "1");
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

      {stage.telemetry !== "unavailable" && (
        // One answer per user of this computer, not a setting of the project: Reset leaves it alone.
        <div className={`${styles.popoverSection} ${styles.row}`}>
          <span className={styles.label}>Share usage data</span>
          <div className={styles.segments} role="radiogroup" aria-label="Share anonymous usage data">
            {([false, true] as const).map((on) => (
              <button key={String(on)} className={`${styles.chip} ${styles.segment}`} role="radio" aria-checked={sharing === on} onClick={() => void setTelemetry(on)}>
                {on ? "On" : "Off"}
              </button>
            ))}
          </div>
        </div>
      )}

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
        <details className={styles.about}>
          <summary className={styles.label}>About &amp; privacy</summary>
          <p>
            Composer by Agogica is free software under the GNU Affero General Public License, version 3. Its source, and the
            licences of what it contains, come with it (LICENSE, THIRD_PARTY_NOTICES).
          </p>
          {stage.telemetry === "unavailable" ? (
            <p>This build sends nothing anywhere.</p>
          ) : (
            <>
              <p>{sharing ? "Anonymous usage data is shared:" : "Nothing is sent. With sharing on, it would be:"}</p>
              <ul>
                {whatIsShared.map((line) => (
                  <li key={line}>{line}</li>
                ))}
              </ul>
              <p>{neverShared}</p>
              {sharing && info?.installId && <p>Sent under the random id {info.installId}; turning sharing off deletes it from this computer.</p>}
            </>
          )}
        </details>
        {(info?.debug || showLoad === "1") && load && (
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
