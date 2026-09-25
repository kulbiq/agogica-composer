import { useEffect, useRef, useState } from "react";
import { getNativeFunction } from "@juce-framework/webview";
import { useJuceEvent } from "./juce";
import styles from "./App.module.css";

type BuildInfo = { version: string; format: string; host: string; credits?: string };

const getBuildInfo = getNativeFunction("getBuildInfo") as () => Promise<BuildInfo>;
const floorDb = -60;

export default function Footer() {
  const [info, setInfo] = useState<BuildInfo>();
  const level = useRef<HTMLDivElement>(null);

  useEffect(() => {
    void getBuildInfo().then(setInfo);
  }, []);

  // 30 Hz from C++; write the DOM directly instead of re-rendering React.
  useJuceEvent<number>("inputLevel", (db) => {
    level.current?.style.setProperty("--level", String(Math.max(0, 1 - db / floorDb)));
  });

  return (
    <footer className={styles.footer}>
      <span className={styles.label} title={info?.credits}>
        {info ? `v${info.version} · ${info.format} · ${info.host}` : " "}
      </span>
      <span className={styles.meter} aria-hidden>
        <span className={styles.label} title={info?.credits}>In</span>
        <span className={styles.meterTrack}>
          <span ref={level} className={styles.meterFill} />
        </span>
      </span>
    </footer>
  );
}
