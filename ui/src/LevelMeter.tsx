import { useEffect, useRef } from "react";
import { useEvent } from "./bridge";
import type { Levels } from "./level";
import { seatLevel } from "./players";
import styles from "./App.module.css";

// Every track meter on show, by its element (the grey track), with the tracks it shows.
const meters = new Map<HTMLElement, number[]>();

// A meter's grey track. Its level is not drawn here but by MeterLayer: changing the page 30 times
// a second, even one transform per meter, makes the browser re-layer the whole page every time
// (with a big project that cost more than the stage itself); a canvas changes nothing in it.
export default function LevelMeter({ ids, className }: { ids: number[]; className?: string }) {
  const track = useRef<HTMLSpanElement>(null);

  useEffect(() => {
    const el = track.current!;
    meters.set(el, ids);
    return () => {
      meters.delete(el);
    };
  }, [ids.join()]); // by value: a new array with the same tracks changes nothing

  return <span ref={track} className={`${styles.meterTrack} ${className ?? ""}`} aria-hidden />;
}

// The meters' levels: one canvas over the visible part of the scrolling list (`children` of
// the scroller it sits in, stuck to its top), drawn at each level event and when the list
// scrolls or changes size. The loudest track of each meter's tracks.
export function MeterLayer() {
  const ref = useRef<HTMLCanvasElement>(null);
  const last = useRef<Levels>({});
  const draw = useRef(() => {});

  useEffect(() => {
    const canvas = ref.current!;
    const scroller = canvas.parentElement!.parentElement!; // the layer's parent scrolls
    const ctx = canvas.getContext("2d")!;
    let ink = "";
    const readInk = () => (ink = getComputedStyle(canvas).getPropertyValue("--ink").trim() || "#0f172a");
    readInk();
    const theme = window.matchMedia("(prefers-color-scheme: dark)");
    theme.addEventListener("change", readInk);

    const size = () => {
      const dpr = window.devicePixelRatio || 1;
      const w = scroller.clientWidth;
      const h = scroller.clientHeight;
      canvas.style.width = `${w}px`;
      canvas.style.height = `${h}px`;
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
      draw.current();
    };
    draw.current = () => {
      const dpr = window.devicePixelRatio || 1;
      const area = canvas.getBoundingClientRect();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = ink;
      for (const [el, ids] of meters) {
        const r = el.getBoundingClientRect();
        if (r.bottom <= area.top || r.top >= area.bottom || r.width === 0) continue;
        const level = seatLevel(last.current, ids);
        if (level <= 0) continue;
        // On whole device pixels, as the meter's track is drawn.
        const x = Math.round((r.left - area.left) * dpr);
        const y = Math.round((r.top - area.top) * dpr);
        const w = Math.round(r.width * level * dpr);
        const h = Math.max(1, Math.round(r.height * dpr));
        ctx.fillRect(x, y, w, h);
      }
    };
    size();
    const ro = new ResizeObserver(size);
    ro.observe(scroller);
    const onScroll = () => draw.current();
    scroller.addEventListener("scroll", onScroll, { passive: true });
    return () => {
      ro.disconnect();
      scroller.removeEventListener("scroll", onScroll);
      theme.removeEventListener("change", readInk);
    };
  }, []);

  useEvent("levels", (levels) => {
    last.current = levels;
    draw.current();
  });

  return (
    <div className={styles.meterLayer} aria-hidden>
      <canvas ref={ref} />
    </div>
  );
}
