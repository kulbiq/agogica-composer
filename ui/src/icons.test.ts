import { expect, test } from "vitest";
import { autoIcon } from "./icons";

test.each([
  ["Violin I", "violin"], ["Vln. 2", "violin"], ["Viola", "violin"], ["Vla", "violin"],
  ["Cello", "cello"], ["Vc.", "cello"], ["Bass", "cello"], ["Double Bass", "cello"],
  ["Flute", "flute"], ["Piccolo", "flute"], ["Alto Flute", "flute"],
  ["Clarinet in Bb", "reed"], ["Bass Clarinet", "reed"], ["Oboe", "reed"], ["English Horn", "reed"], ["Tenor Sax", "reed"],
  ["Horn 1", "horn"], ["Hn. 2", "horn"],
  ["Trumpet in C", "trumpet"], ["Bass Trombone", "trumpet"], ["Tuba", "trumpet"],
  ["Timpani", "timpani"], ["Bass Drum", "drum"], ["Percussion", "drum"], ["Glockenspiel", "drum"],
  ["Piano", "piano"], ["Pno. RH", "piano"], ["Harpsichord", "piano"], ["Organ", "organ"], ["Harp", "harp"],
  ["Soprano", "voice"], ["Tenor", "voice"], ["Choir", "voice"],
  ["Track 7", "other"], ["", "other"],
])("%s -> %s", (name, icon) => {
  expect(autoIcon(name)).toBe(icon);
});
