import { useEffect, useState, type MouseEvent } from "react";
import styles from "./App.module.css";
import LevelMeter from "./LevelMeter";
import { getBuildInfo, useEvent, type BuildInfo, type Load } from "./bridge";

const percent = (v: number) => `${(v * 100).toFixed(v < 0.1 ? 1 : 0)}%`;
const loadKey = "showLoad";

// `player`: whose input the meter shows (the selected player).
export default function Footer({ player }: { player?: { id: number; name: string } }) {
  const [info, setInfo] = useState<BuildInfo>();
  const [load, setLoad] = useState<Load>();
  // For us only: always in Debug builds; in Release, Alt+click the version to toggle.
  const [showLoad, setShowLoad] = useState(() => localStorage.getItem(loadKey) === "1");

  useEffect(() => {
    void getBuildInfo().then(setInfo);
  }, []);
  useEvent("load", setLoad);

  const toggleLoad = (e: MouseEvent) => {
    if (!e.altKey) return;
    localStorage.setItem(loadKey, showLoad ? "0" : "1");
    setShowLoad(!showLoad);
  };

  return (
    <footer className={styles.footer}>
      <span className={styles.label} title={info?.credits} onClick={toggleLoad}>
        {info ? `v${info.version} · ${info.format} · ${info.host}` : " "}
      </span>
      {(info?.debug || showLoad) && load && (
        <span className={`${styles.label} ${styles.load}`} title="processBlock's share of one core: this track / all Composer tracks (peak = the worst block)">
          DSP {percent(load.self.average)} · peak {percent(load.self.peak)} · {load.all.tracks}{" "}
          {load.all.tracks === 1 ? "track" : "tracks"} {percent(load.all.average)}
          {load.all.overruns > 0 && ` · over ${load.all.overruns}`}
        </span>
      )}
      {player && (
        <span className={styles.meter} aria-hidden>
          <span className={`${styles.label} ${styles.meterName}`}>In · {player.name}</span>
          <LevelMeter key={player.id} id={player.id} />
        </span>
      )}
    </footer>
  );
}
