import styles from "./App.module.css";
import Footer from "./Footer";
import ResizeGrip from "./ResizeGrip";
import RoomSizeSlider from "./RoomSizeSlider";

export default function App() {
  return (
    <div className={styles.root}>
      <main className={styles.main}>
        <span className={styles.label}>Agogica · Composer</span>
        <h1 className={styles.title}>Hello, stage.</h1>
        <p className={styles.lead}>
          Place every player in the room and hear the ensemble as it stands there.
        </p>
        <RoomSizeSlider />
      </main>
      <Footer />
      <ResizeGrip className={styles.grip} />
    </div>
  );
}
