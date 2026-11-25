import { setCameraPreviewEnabled, type TrackingFrame } from "./bridge";
import { elementById } from "./dom";

const SECTION_OPEN_KEY = "cameraOpen";

// The Camera section: the picture comes from the app's own camera (a second reader isn't
// allowed on Windows), sent only while the section is open; it stays as the user left it.
export function setUpCameraSection() {
  const section = document.querySelector<HTMLDetailsElement>("details.more");
  if (!section) throw new Error("The page has no camera section");

  section.open = localStorage.getItem(SECTION_OPEN_KEY) === "1";
  section.ontoggle = () => {
    localStorage.setItem(SECTION_OPEN_KEY, section.open ? "1" : "0");
    void setCameraPreviewEnabled(section.open);
  };
  void setCameraPreviewEnabled(section.open);
}

export function showCameraPicture(jpegDataUrl: string) {
  const picture = elementById<HTMLImageElement>("frame");
  picture.src = jpegDataUrl;
  picture.hidden = false;
}

// The head the app follows, over the picture (the two are mirrored together, in tracker.css).
export function showHeadBox(headBox: TrackingFrame["headBox"]) {
  const box = elementById("box");
  box.hidden = headBox === undefined;
  if (!headBox) return;

  const [x, y, width, height] = headBox;
  Object.assign(box.style, {
    left: `${x * 100}%`,
    top: `${y * 100}%`,
    width: `${width * 100}%`,
    height: `${height * 100}%`,
  });
}
