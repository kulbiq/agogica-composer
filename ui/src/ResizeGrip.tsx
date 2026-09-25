import { useRef, type PointerEvent } from "react";
import { getNativeFunction } from "@juce-framework/webview";

const setEditorSize = getNativeFunction("setEditorSize");

// Bottom-right resize handle. Hosts that don't resize plugin windows themselves
// (e.g. Ableton Live) rely on it; C++ applies the editor's size limits.
export default function ResizeGrip({ className }: { className?: string }) {
  const drag = useRef<{ x: number; y: number; w: number; h: number } | null>(null);
  const frame = useRef(0);

  const onPointerDown = (e: PointerEvent) => {
    e.currentTarget.setPointerCapture(e.pointerId);
    drag.current = { x: e.screenX, y: e.screenY, w: window.innerWidth, h: window.innerHeight };
  };

  const onPointerMove = (e: PointerEvent) => {
    const start = drag.current;
    if (!start) return;
    // Screen coordinates: the page itself moves under the pointer while resizing.
    const w = Math.round(start.w + e.screenX - start.x);
    const h = Math.round(start.h + e.screenY - start.y);
    cancelAnimationFrame(frame.current);
    frame.current = requestAnimationFrame(() => void setEditorSize(w, h));
  };

  const onPointerUp = () => {
    drag.current = null;
  };

  return (
    <div
      className={className}
      aria-hidden
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
    >
      <svg viewBox="0 0 12 12" width="12" height="12">
        <path d="M11 4 4 11M11 8 8 11" />
      </svg>
    </div>
  );
}
