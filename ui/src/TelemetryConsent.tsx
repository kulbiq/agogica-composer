import { useEffect, useRef } from "react";
import styles from "./App.module.css";
import { setTelemetry } from "./bridge";

// What is shared after a yes, in the words of PRIVACY.md (Source/telemetry/Telemetry.cpp sends
// exactly this; the list here and there are changed together).
export const whatIsShared = [
  "Composer's version, the plugin format, the host application, the system and its version",
  "How a session went: its length, how many tracks, Stereo or Binaural, the room size, scenes used, whether head tracking ran",
  "Audio performance: sample rate, buffer size, Composer's share of the processor",
  "Errors of this window, if it ever stops drawing",
];
export const neverShared = "Never: names of tracks, players or projects, positions, sound, anything from the camera.";

// Asked once per computer, the first time a Composer window opens (and again only if it was
// closed without an answer). Either answer can be changed in the settings.
export default function TelemetryConsent() {
  const first = useRef<HTMLButtonElement>(null);
  useEffect(() => first.current?.focus(), []);

  return (
    <div className={styles.consent} role="dialog" aria-labelledby="consent-title" aria-describedby="consent-text">
      <span className={styles.label}>Before you start</span>
      <h2 id="consent-title" className={styles.consentTitle}>
        Help improve Composer?
      </h2>
      <p id="consent-text" className={styles.consentText}>
        Composer can send anonymous usage data, so we learn which hosts and setups it runs in and where it struggles. Nothing is
        sent unless you agree.
      </p>
      <details className={styles.consentDetails}>
        <summary className={styles.label}>What is sent</summary>
        <ul>
          {whatIsShared.map((line) => (
            <li key={line}>{line}</li>
          ))}
        </ul>
        <p>{neverShared}</p>
      </details>
      <div className={styles.consentButtons}>
        <button className={styles.ghost} ref={first} onClick={() => void setTelemetry(false)}>
          No thanks
        </button>
        <button className={styles.primary} onClick={() => void setTelemetry(true)}>
          Share anonymous usage data
        </button>
      </div>
    </div>
  );
}
