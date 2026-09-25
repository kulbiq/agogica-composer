import { useState } from "react";
import styles from "./App.module.css";
import Footer from "./Footer";
import { useStage } from "./juce";
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

  return (
    <div className={styles.root}>
      {stage && selfId !== undefined ? (
        <div className={styles.body}>
          <StageMap stage={stage} selfId={selfId} selected={selected} onSelect={setPicked} />
          <SidePanel stage={stage} selfId={selfId} selected={selected} onSelect={setPicked} />
        </div>
      ) : (
        <div className={styles.body} />
      )}
      <Footer />
      <ResizeGrip className={styles.grip} />
    </div>
  );
}
