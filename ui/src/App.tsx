import { useEffect, useState } from "react";
import styles from "./App.module.css";
import { useStage } from "./bridge";
import PlayersPanel from "./PlayersPanel";
import { usePref } from "./prefs";
import ResizeGrip from "./ResizeGrip";
import StageMap, { type MapNames, type MapView, type NodeKey } from "./StageMap";
import TopBar from "./TopBar";

const views = ["grid", "azimuth"] as const satisfies readonly MapView[];
const nameModes = ["selected", "all"] as const satisfies readonly MapNames[];

export default function App() {
  const stage = useStage();
  const selfId = stage?.players.find((p) => p.self)?.id;
  const [picked, setPicked] = useState<NodeKey | null>(null);
  const [view, setView] = usePref<MapView>("mapView", "grid", views);
  const [names, setNames] = usePref<MapNames>("mapNames", "selected", nameModes);

  // Own player is selected until the user picks something; a vanished player drops the selection.
  const exists = (key: NodeKey | null) => key === "listener" || stage?.players.some((p) => p.id === key);
  const selected = picked !== null && exists(picked) ? picked : (selfId ?? null);

  // One Composer window is enough (each shows every track), but a plugin can't close host
  // windows. So a window opened while another is open steps aside until the user insists
  // or the other one closes; after that it stays open for good.
  const [blocked, setBlocked] = useState<boolean | null>(null);
  useEffect(() => {
    if (stage) setBlocked((b) => (b ?? true) && stage.openElsewhere !== undefined);
  }, [stage]);

  return (
    <div className={styles.root}>
      {stage && blocked && stage.openElsewhere !== undefined ? (
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
        <>
          <TopBar stage={stage} selfId={selfId} names={names} onNames={setNames} />
          <div className={styles.body}>
            <PlayersPanel stage={stage} selected={selected} onSelect={setPicked} />
            <div className={styles.mapArea}>
              <StageMap stage={stage} selfId={selfId} selected={selected} onSelect={setPicked} view={view} names={names} />
              <div className={`${styles.segments} ${styles.viewSwitch}`} role="radiogroup" aria-label="View">
                {views.map((v) => (
                  <button key={v} className={`${styles.chip} ${styles.segment}`} role="radio" aria-checked={view === v} onClick={() => setView(v)}>
                    {v === "grid" ? "Grid" : "Azimuth"}
                  </button>
                ))}
              </div>
            </div>
          </div>
        </>
      ) : (
        <div className={styles.body} />
      )}
      <ResizeGrip className={styles.grip} />
    </div>
  );
}
