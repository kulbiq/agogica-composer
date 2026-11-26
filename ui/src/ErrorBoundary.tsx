import { Component, type ErrorInfo, type ReactNode } from "react";
import styles from "./App.module.css";
import { reportError } from "./bridge";

// An error while drawing the page would otherwise leave an empty window until the host opens
// the editor again. The sound never depends on the page: this says so and offers a reload.
export default class ErrorBoundary extends Component<{ children: ReactNode }, { failed: boolean }> {
  state = { failed: false };

  static getDerivedStateFromError() {
    return { failed: true };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    void reportError(String(error), `${error.stack ?? ""}\n${info.componentStack ?? ""}`, "render");
  }

  render() {
    if (!this.state.failed) return this.props.children;
    return (
      <div className={styles.root}>
        <div className={`${styles.body} ${styles.elsewhere}`} role="alert">
          <span className={styles.label}>Something went wrong</span>
          <h1 className={styles.elsewhereTitle}>This window stopped drawing</h1>
          <p className={styles.elsewhereText}>The sound and your project are not affected. Reloading brings the window back.</p>
          <button className={styles.ghost} onClick={() => location.reload()}>
            Reload
          </button>
        </div>
      </div>
    );
  }
}
