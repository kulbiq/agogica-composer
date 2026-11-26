// Instrument icons: one hairline set on a 24 px grid, drawn as SVG in the panel and as
// Path2D on the stage map, from the same path data.

export type IconKey =
  | "violin" | "cello" | "flute" | "reed" | "horn" | "trumpet" | "timpani"
  | "drum" | "piano" | "organ" | "harp" | "voice" | "other";

type Part = { d: string; fill?: boolean };
type Shape = { rot?: number; parts: Part[] };

const circle = (x: number, y: number, r: number) => `M${x - r} ${y}a${r} ${r} 0 1 0 ${2 * r} 0a${r} ${r} 0 1 0 ${-2 * r} 0`;
const ellipse = (x: number, y: number, rx: number, ry: number) =>
  `M${x - rx} ${y}a${rx} ${ry} 0 1 0 ${2 * rx} 0a${rx} ${ry} 0 1 0 ${-2 * rx} 0`;
const rect = (x: number, y: number, w: number, h: number) => `M${x} ${y}h${w}v${h}h${-w}Z`;
const rounded = (x: number, y: number, w: number, h: number, k: number) =>
  `M${x + k} ${y}h${w - 2 * k}a${k} ${k} 0 0 1 ${k} ${k}v${h - 2 * k}a${k} ${k} 0 0 1 ${-k} ${k}` +
  `h${-(w - 2 * k)}a${k} ${k} 0 0 1 ${-k} ${-k}v${-(h - 2 * k)}a${k} ${k} 0 0 1 ${k} ${-k}Z`;
const S = (d: string): Part => ({ d });
const F = (d: string): Part => ({ d, fill: true });

const shapes: Record<IconKey, Shape> = {
  violin: { parts: [S(circle(12, 2.6, 1.1)), S("M12 3.7V15"),
    S("M12 8C9 8 8.1 9.2 8.5 10.8C8.8 12 9.4 12.6 9 13.6C7.6 14.6 7.2 16.4 7.8 18.2C8.6 20.6 10.6 21.5 12 21.5C13.4 21.5 15.4 20.6 16.2 18.2C16.8 16.4 16.4 14.6 15 13.6C14.6 12.6 15.2 12 15.5 10.8C15.9 9.2 15 8 12 8Z"),
    S("M10.2 14.6V17M13.8 14.6V17M10.6 18.3H13.4")] },
  cello: { parts: [S(circle(12, 1.8, 1)), S("M12 2.8V13.8"),
    S("M12 6.4C8.6 6.4 7.6 7.8 8 9.6C8.4 11 9 11.7 8.6 12.7C7 13.8 6.6 15.7 7.2 17.6C8 20 10.3 20.8 12 20.8C13.7 20.8 16 20 16.8 17.6C17.4 15.7 17 13.8 15.4 12.7C15 11.7 15.6 11 16 9.6C16.4 7.8 15.4 6.4 12 6.4Z"),
    S("M10 13.6V16.2M14 13.6V16.2M10.4 17.6H13.6M12 20.8V23")] },
  flute: { rot: -30, parts: [S(rounded(1.5, 10.6, 21, 2.8, 1.4)), F(ellipse(4.6, 12, 0.8, 0.5)),
    F(circle(10, 12, 0.6)), F(circle(12.6, 12, 0.6)), F(circle(15.2, 12, 0.6)), F(circle(17.8, 12, 0.6))] },
  reed: { rot: 28, parts: [S("M10.9 3.5L11.6 1.5H12.4L13.1 3.5"), S(rounded(10.6, 3.5, 2.8, 14, 0.6)), S("M10.6 6H13.4"),
    S("M10.6 17.5L9 21.5H15L13.4 17.5"), F(circle(12, 9, 0.6)), F(circle(12, 11.5, 0.6)), F(circle(12, 14, 0.6))] },
  horn: { parts: [S(circle(10, 12, 6.2)), S(circle(10, 12, 3.4)), S("M13.8 16.9L17.8 20.6M15.6 14.6L21 17.3"),
    S("M17.8 20.6C19.8 21.6 21.8 19.6 21 17.3"), S("M6.6 6.8L4 3.2M3.3 2.6L4.6 3.8")] },
  trumpet: { parts: [S("M2.5 10.2L3.6 11L2.5 11.8"), S("M3.6 11H14M5.5 13H14"),
    S("M14 11C16.5 11 18.8 9.6 21.5 7.5V16.5C18.8 14.4 16.5 13 14 13"),
    S("M8 8.4V11M10.3 8.4V11M12.6 8.4V11M7.3 8.4H8.7M9.6 8.4H11M11.9 8.4H13.3"),
    S("M5.5 13V15.2C5.5 16.3 6.2 17 7.3 17H12.3C13.4 17 14 16.3 14 15.2V13")] },
  timpani: { parts: [S(ellipse(12, 9.5, 8.5, 2.4)), S("M3.5 9.5C3.5 15 7.3 18 12 18C16.7 18 20.5 15 20.5 9.5"),
    S("M8.5 17.5L7 21.5M15.5 17.5L17 21.5"), S("M20.5 1.8L15.8 6.2"), F(circle(15, 7, 1.1))] },
  drum: { parts: [S(ellipse(12, 9, 8, 2.4)), S("M4 9V16C4 17.3 7.6 18.6 12 18.6C16.4 18.6 20 17.3 20 16V9"),
    S("M4 11.5L8 17.9L12 11.4L16 17.9L20 11.5"), S("M5.5 2L10.5 6.2M18.5 2L13.5 6.2")] },
  piano: { parts: [S(rounded(2.5, 6, 19, 12, 1)), S("M5.21 13V18M7.93 13V18M10.64 6V18M13.36 13V18M16.07 13V18M18.79 13V18"),
    F(rect(4.41, 6, 1.6, 7)), F(rect(7.13, 6, 1.6, 7)), F(rect(12.56, 6, 1.6, 7)), F(rect(15.27, 6, 1.6, 7)), F(rect(17.99, 6, 1.6, 7))] },
  organ: { parts: [S(rect(2.5, 18.5, 19, 3)), S(rect(3.3, 10, 2.4, 8.5)), S(rect(7.05, 6.5, 2.4, 12)), S(rect(10.8, 3, 2.4, 15.5)),
    S(rect(14.55, 6.5, 2.4, 12)), S(rect(18.3, 10, 2.4, 8.5)), S("M4 15.5H5M7.75 15.5H8.75M11.5 15.5H12.5M15.25 15.5H16.25M19 15.5H20")] },
  harp: { parts: [S("M6.5 21.5V4.2"), S("M6.5 4.2C9 2.6 11.5 6.4 15 5.2C16.6 4.6 17.6 3.6 19 3.4"),
    S("M19 3.4L8.5 21.5M4.8 21.5H10.2"), S("M9.5 4.3V19.8M12 5.1V15.5M14.5 5.4V11.2")] },
  voice: { parts: [S(rounded(9, 2.5, 6, 11, 3)), S("M6.5 10.5C6.5 13.8 9 16.5 12 16.5C15 16.5 17.5 13.8 17.5 10.5"),
    S("M12 16.5V20.5M8.5 20.5H15.5")] },
  other: { parts: [F(circle(12, 12, 3))] },
};

export const iconKeys = Object.keys(shapes) as IconKey[];

// Picked from the track name; the first rule that matches wins, so the specific ones
// (bass clarinet, english horn, bass drum) come before the general ones (clarinet, horn, bass).
const rules: [RegExp, IconKey][] = [
  [/\btimp/, "timpani"],
  [/\b(perc\w*|drums?|snare|cymbals?|tam|tambourine|triangle|glock\w*|xylo\w*|marimba|vib\w*)\b/, "drum"],
  [/\b(org|organ)\b/, "organ"],
  [/\b(pno|pf|piano|celesta|cel|harpsichord|hpsd|keys|keyboard)\b/, "piano"],
  [/\b(hp|harp)\b/, "harp"],
  [/\b(fl|flt|flutes?|picc|piccolo)\b/, "flute"],
  [/\b(ob|oboes?|cl|clar|clarinets?|bn|bsn|bassoons?|sax\w*|eh|english horn|cor anglais)\b/, "reed"],
  [/\b(hn|horns?|cor)\b/, "horn"],
  [/\b(tpt|trp|trumpets?|tbn|trb|trombones?|tuba|tba|cornet|flugel\w*|euphonium|brass)\b/, "trumpet"],
  [/\b(vc|vlc|cellos?|violoncello|cb|db|kb|contrabass|double bass|bass)\b/, "cello"],
  [/\b(vn|vln|vl|violins?|vla|va|violas?|strings|str)\b/, "violin"],
  [/\b(sop|soprano|mezzo|alt|alto|ten|tenor|bar|bari|baritone|choir|chorus|voice|vox|vocals?)\b/, "voice"],
];

export function autoIcon(name: string): IconKey {
  const words = ` ${name.toLowerCase().replace(/[^a-z]+/g, " ").trim()} `;
  return rules.find(([re]) => re.test(words))?.[1] ?? "other";
}

export function Icon({ icon, size = 16, className }: { icon: IconKey; size?: number; className?: string }) {
  const { rot, parts } = shapes[icon];
  return (
    <svg className={className} width={size} height={size} viewBox="0 0 24 24" aria-hidden
      fill="none" stroke="currentColor" strokeWidth={1.25} strokeLinecap="round" strokeLinejoin="round">
      <g transform={rot ? `rotate(${rot} 12 12)` : undefined}>
        {parts.map((p, i) => (p.fill ? <path key={i} d={p.d} fill="currentColor" stroke="none" /> : <path key={i} d={p.d} />))}
      </g>
    </svg>
  );
}

// Canvas drawing: Path2D objects are made once, on first use.
let paths: Map<IconKey, { rot: number; parts: { path: Path2D; fill: boolean }[] }> | undefined;

export function drawIcon(ctx: CanvasRenderingContext2D, icon: IconKey, x: number, y: number, size: number, colour: string) {
  paths ??= new Map(iconKeys.map((k) => [k, { rot: shapes[k].rot ?? 0, parts: shapes[k].parts.map((p) => ({ path: new Path2D(p.d), fill: !!p.fill })) }]));
  const { rot, parts } = paths.get(icon)!;
  ctx.save();
  ctx.translate(x, y);
  ctx.scale(size / 24, size / 24);
  if (rot) ctx.rotate((rot * Math.PI) / 180);
  ctx.translate(-12, -12);
  ctx.lineWidth = 1.35;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.strokeStyle = colour;
  ctx.fillStyle = colour;
  for (const p of parts) {
    if (p.fill) ctx.fill(p.path);
    else ctx.stroke(p.path);
  }
  ctx.restore();
}
