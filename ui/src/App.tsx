import { useEffect, useState } from "react";
import styles from "./App.module.css";
import Footer from "./Footer";
import { checkLicence, openHub, useStage } from "./juce";
import ResizeGrip from "./ResizeGrip";
import SidePanel from "./SidePanel";
import StageMap, { type NodeKey } from "./StageMap";

export default function App() {
  const stage = useStage();
  const selfId = stage?.players.find((p) => p.self)?.id;
  const [picked, setPicked] = useState<NodeKey | null>(null);

  // Own player is selected until the user picks something; a vanished player drops the selection.
  const exists = (key: NodeKey | null) => key === "listener" || stage?.players.some((p) => p.id === key);
  const selected = picked !== null && exists(picked) ? picked : (selfId ?? null);
  const metered = stage?.players.find((p) => p.id === (typeof selected === "number" ? selected : selfId));

  // One Composer window is enough (each shows every track), but a plugin can't close host
  // windows. So a window opened while another is open steps aside until the user insists
  // or the other one closes; after that it stays open for good.
  const [blocked, setBlocked] = useState<boolean | null>(null);
  useEffect(() => {
    if (stage) setBlocked((b) => (b ?? true) && stage.openElsewhere !== undefined);
  }, [stage]);

  return (
    <div className={styles.root}>
      {stage?.licence && !stage.licence.ok ? (
        // Not activated: audio passes through untouched until Agogica Hub writes a licence.
        <div className={`${styles.body} ${styles.elsewhere}`}>
          <span className={styles.label}>Composer is not active</span>
          <h1 className={styles.elsewhereTitle}>Open Agogica Hub</h1>
          <p className={styles.elsewhereText}>
            {stage.licence.message} Sign in to Agogica Hub once while online to activate it; after that it works
            offline. Until then, audio passes through unchanged.
          </p>
          <div className={styles.licenceActions}>
            <button className={styles.ghost} onClick={() => void openHub()}>
              Open Agogica Hub
            </button>
            <button className={styles.ghost} onClick={() => void checkLicence()}>
              Check again
            </button>
          </div>
        </div>
      ) : stage && blocked && stage.openElsewhere !== undefined ? (
        <div className={`${styles.body} ${styles.elsewhere}`}>
          <span className={styles.label}>Composer is already open</span>
          <h1 className={styles.elsewhereTitle}>{stage.openElsewhere}</h1>
          <p className={styles.elsewhereText}>
            Every Composer window shows the whole stage. Close this one and continue in the window of{" "}
            {stage.openElsewhere}.
          </p>
          <button className={styles.ghost} onClick={() => setBlocked(false)}>
            Open here anyway
          </button>
        </div>
      ) : stage && selfId !== undefined && blocked === false ? (
        <div className={styles.body}>
          <StageMap stage={stage} selfId={selfId} selected={selected} onSelect={setPicked} />
          <SidePanel stage={stage} selfId={selfId} selected={selected} onSelect={setPicked} />
        </div>
      ) : (
        <div className={styles.body} />
      )}
      <Footer player={metered} />
      <ResizeGrip className={styles.grip} />
    </div>
  );
}
