import styles from "./App.module.css";
import ResizeGrip from "./ResizeGrip";

const version = import.meta.env.VITE_COMPOSER_VERSION ?? "dev";

export default function App() {
  return (
    <main className={styles.root}>
      <span className={styles.label}>Agogica · Composer</span>
      <h1 className={styles.title}>Hello, stage.</h1>
      <p className={styles.lead}>
        Place every player in the room and hear the ensemble as it stands there.
      </p>
      <span className={styles.label}>v{version}</span>
      <ResizeGrip className={styles.grip} />
    </main>
  );
}
