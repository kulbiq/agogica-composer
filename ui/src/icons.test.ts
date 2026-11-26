import { expect, test } from "vitest";
import cpp from "../../Source/stage/StateRules.h?raw";
import { autoIcon, iconKeys, playerIcon } from "./icons";

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

test("a chosen icon wins over the name; empty or unknown falls back to it", () => {
  expect(playerIcon({ icon: "harp", name: "Violin" })).toBe("harp");
  expect(playerIcon({ icon: "", name: "Violin" })).toBe("violin");
  expect(playerIcon({ icon: "banjo", name: "Violin" })).toBe("violin");
  expect(playerIcon({ icon: "", name: "Woodwinds", tracks: [{ track: "Track 3" }, { track: "Flute" }] })).toBe("flute");
  expect(playerIcon({ icon: "", name: "Horns", tracks: [{ track: "Flute" }] })).toBe("horn"); // its own name first
});

test("the plugin accepts exactly these icon keys (Source/stage/StateRules.h)", () => {
  const list = /known \{([^}]*)\}/.exec(cpp)![1];
  expect([...list.matchAll(/"(\w*)"/g)].map((m) => m[1])).toEqual(["", ...iconKeys]);
});
