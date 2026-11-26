import { useEffect, useRef, useState } from "react";
import styles from "./App.module.css";
import { setIcon, setName, type Player } from "./bridge";
import { autoIcon, Icon, iconKeys, iconLabels, type IconKey } from "./icons";

// One player's name and icon, in a sheet that opens from the players panel over the map.
// The icon applies at once; the name on Enter, Done, Back, Escape or a press outside.
// An empty name goes back to the track's.
export default function EditPlayer({ player, onBack }: { player: Player; onBack: () => void }) {
  const [draft, setDraft] = useState(player.name);
  const root = useRef<HTMLDivElement>(null);

  const commitName = () => {
    if (draft.trim() !== player.name) void setName(player.id, draft.trim());
  };
  const close = () => {
    commitName();
    onBack();
  };

  const latestClose = useRef(close);
  latestClose.current = close;
  useEffect(() => {
    const onPointerDown = (e: PointerEvent) => !root.current?.contains(e.target as Node) && latestClose.current();
    const onKeyDown = (e: KeyboardEvent) => e.key === "Escape" && latestClose.current();
    document.addEventListener("pointerdown", onPointerDown);
    document.addEventListener("keydown", onKeyDown);
    return () => {
      document.removeEventListener("pointerdown", onPointerDown);
      document.removeEventListener("keydown", onKeyDown);
    };
  }, []);

  const cell = (key: IconKey | "", icon: IconKey, label: string, title: string) => (
    <button
      key={key}
      className={styles.iconCell}
      role="radio"
      aria-checked={player.icon === key}
      aria-label={title}
      title={title}
      onClick={() => void setIcon(player.id, key)}
    >
      <Icon icon={icon} size={28} />
      <span>{label}</span>
    </button>
  );

  return (
    <div className={styles.sheet} ref={root} role="dialog" aria-label="Edit player">
      <div className={styles.sheetHead}>
        <button className={styles.back} aria-label="Back to players" onClick={close}>
          <svg width="14" height="14" viewBox="0 0 10 10" aria-hidden>
            <path d="M6.5 2L3.5 5L6.5 8" />
          </svg>
        </button>
        <span className={`${styles.label} ${styles.strong}`}>Edit player</span>
        {player.self && <span className={styles.you}>This track</span>}
      </div>

      <div className={styles.sheetBody}>
        <label className={styles.field}>
          <span className={styles.label}>Name</span>
          <input
            className={styles.textInput}
            value={draft}
            maxLength={64}
            autoFocus
            onFocus={(e) => e.currentTarget.select()}
            onChange={(e) => setDraft(e.target.value)}
            onBlur={commitName}
            onKeyDown={(e) => e.key === "Enter" && e.currentTarget.blur()}
          />
        </label>

        <div className={styles.field}>
          <span className={styles.label} id="icon-label">
            Icon
          </span>
          <div className={styles.iconGrid} role="radiogroup" aria-labelledby="icon-label">
            {cell("", autoIcon(player.name), "Auto", `Automatic: ${iconLabels[autoIcon(player.name)]}`)}
            {iconKeys.map((key) => cell(key, key, iconLabels[key].split(",")[0], iconLabels[key]))}
          </div>
          <p className={styles.note}>Auto picks the icon from the track name (pno, vln, hn…).</p>
        </div>
      </div>

      <div className={styles.sheetFoot}>
        <button className={styles.primary} onClick={close}>
          Done
        </button>
      </div>
    </div>
  );
}
