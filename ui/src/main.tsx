import "./devMock";
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import "./styles/fonts.css";
import "./styles/tokens.css";
import "./styles/room.css";
import App from "./App";
import { reportError } from "./bridge";
import ErrorBoundary from "./ErrorBoundary";

// Errors outside React's drawing (event handlers, promises): reported, the window goes on.
window.addEventListener("error", (e) => void reportError(e.message, e.error instanceof Error ? (e.error.stack ?? "") : "", "window"));
window.addEventListener("unhandledrejection", (e) => {
  const reason: unknown = e.reason;
  void reportError(String(reason), reason instanceof Error ? (reason.stack ?? "") : "", "promise");
});

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <ErrorBoundary>
      <App />
    </ErrorBoundary>
  </StrictMode>,
);
