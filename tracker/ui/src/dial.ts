import { elementById } from "./dom";

const SVG = "http://www.w3.org/2000/svg";
// In the dial's own units: the SVG's viewBox is 120 wide, the ring's radius 50.
const RING_RADIUS = 50;
const TICK_INNER_RADIUS = 46.5;
const MAJOR_TICK_INNER_RADIUS = 44;
const LABEL_RADIUS = 38.5;
const TICK_EVERY_DEGREES = 30;
const MAJOR_TICK_EVERY_DEGREES = 90;

// The dial's grid: a tick every 30 degrees (longer every 90) inside the ring, each labelled in
// Composer's convention (clockwise positive = head turned right; 180 behind).
export function buildDialGrid() {
  const grid = elementById("grid");
  for (let degrees = -180 + TICK_EVERY_DEGREES; degrees <= 180; degrees += TICK_EVERY_DEGREES) {
    const radians = (degrees * Math.PI) / 180;
    const x = Math.sin(radians), y = -Math.cos(radians); // 0 at the top, clockwise
    const isMajor = degrees % MAJOR_TICK_EVERY_DEGREES === 0;
    const innerRadius = isMajor ? MAJOR_TICK_INNER_RADIUS : TICK_INNER_RADIUS;

    const tick = document.createElementNS(SVG, "line");
    tick.setAttribute("class", isMajor ? "tick major" : "tick");
    tick.setAttribute("x1", String(x * innerRadius));
    tick.setAttribute("y1", String(y * innerRadius));
    tick.setAttribute("x2", String(x * RING_RADIUS));
    tick.setAttribute("y2", String(y * RING_RADIUS));

    const label = document.createElementNS(SVG, "text");
    label.setAttribute("class", "angle");
    label.setAttribute("x", String(x * LABEL_RADIUS));
    label.setAttribute("y", String(y * LABEL_RADIUS));
    label.textContent = degrees < 0 ? `−${-degrees}` : String(degrees);
    grid.append(tick, label);
  }
}

export function turnDialTo(yawDegrees: number) {
  elementById("head").setAttribute("transform", `rotate(${yawDegrees})`);
}
