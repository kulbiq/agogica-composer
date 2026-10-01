import { useEffect, useState } from "react";
import { getNativeFunction } from "@juce-framework/webview";
import styles from "./App.module.css";
import LevelMeter from "./LevelMeter";

type BuildInfo = { version: string; format: string; host: string; credits?: string };

const getBuildInfo = getNativeFunction("getBuildInfo") as () => Promise<BuildInfo>;
// `player`: whose input the meter shows (the selected player).
export default function Footer({ player }: { player?: { id: number; name: string } }) {
  const [info, setInfo] = useState<BuildInfo>();

  useEffect(() => {
    void getBuildInfo().then(setInfo);
  }, []);

  return (
    <footer className={styles.footer}>
      <span className={styles.label} title={info?.credits}>
        {info ? `v${info.version} · ${info.format} · ${info.host}` : " "}
      </span>
      {player && (
        <span className={styles.meter} aria-hidden>
          <span className={`${styles.label} ${styles.meterName}`}>In · {player.name}</span>
          <LevelMeter key={player.id} id={player.id} />
        </span>
      )}
    </footer>
  );
}
