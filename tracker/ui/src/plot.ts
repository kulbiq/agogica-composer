// The last seconds of yaw, a dot per camera frame, fainter where the model is less sure.
const PLOT_WINDOW_MS = 10_000; // the caption in index.html says "Last 10 s"
const DOT_SIZE_PIXELS = 3;
const VERTICAL_MARGIN_PIXELS = 6;
const GRID_LINES_DEGREES = [-180, -90, 0, 90, 180]; // 0 = straight ahead
const MAX_PIXEL_RATIO = 2;
const FALLBACK_INK_RGB = "15,23,42"; // --ink-rgb's light-theme value, if the token is missing

type PlotPoint = { timeMs: number; yawDegrees: number; confidence: number };

export function startPlot(canvas: HTMLCanvasElement) {
  const context = canvas.getContext("2d");
  if (!context) throw new Error("No 2D canvas");
  const points: PlotPoint[] = [];

  const sizeCanvasToElement = () => {
    const pixelRatio = Math.min(window.devicePixelRatio || 1, MAX_PIXEL_RATIO);
    const width = Math.round(canvas.clientWidth * pixelRatio), height = Math.round(canvas.clientHeight * pixelRatio);
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    context.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
  };

  const draw = () => {
    requestAnimationFrame(draw);
    sizeCanvasToElement();
    const width = canvas.clientWidth, height = canvas.clientHeight;
    context.clearRect(0, 0, width, height);

    const inkRgb = getComputedStyle(canvas).getPropertyValue("--ink-rgb").trim() || FALLBACK_INK_RGB;
    const now = performance.now();
    const xOf = (timeMs: number) => width - ((now - timeMs) / PLOT_WINDOW_MS) * width;
    const yOf = (degrees: number) => height / 2 - (degrees / 180) * (height / 2 - VERTICAL_MARGIN_PIXELS);

    context.lineWidth = 1;
    for (const degrees of GRID_LINES_DEGREES) {
      const y = Math.round(yOf(degrees)) + 0.5; // on the pixel grid: a crisp hairline
      context.strokeStyle = `rgba(${inkRgb},${degrees === 0 ? 0.18 : 0.08})`;
      context.beginPath();
      context.moveTo(0, y);
      context.lineTo(width, y);
      context.stroke();
    }

    for (const point of points) {
      context.fillStyle = `rgba(${inkRgb},${0.2 + 0.7 * point.confidence})`;
      context.fillRect(xOf(point.timeMs) - DOT_SIZE_PIXELS / 2, yOf(point.yawDegrees) - DOT_SIZE_PIXELS / 2, DOT_SIZE_PIXELS, DOT_SIZE_PIXELS);
    }
  };
  requestAnimationFrame(draw);

  return {
    add(yawDegrees: number, confidence: number) {
      const now = performance.now();
      while (points.length && now - points[0].timeMs > PLOT_WINDOW_MS) points.shift();
      points.push({ timeMs: now, yawDegrees, confidence });
    },
  };
}
