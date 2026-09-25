// Copies MediaPipe's WASM runtime and the face model next to the built page, so the tracker
// works offline. The model is downloaded once into models/ (not committed; Apache-2.0,
// https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/).
import { cpSync, existsSync, mkdirSync, writeFileSync } from "node:fs";

const modelUrl = "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task";
const model = "models/face_landmarker.task";

if (!existsSync(model)) {
  mkdirSync("models", { recursive: true });
  const response = await fetch(modelUrl);
  if (!response.ok) throw new Error(`model download failed: ${response.status}`);
  writeFileSync(model, Buffer.from(await response.arrayBuffer()));
}

// forVisionTasks() loads vision_wasm_internal (SIMD) or vision_wasm_nosimd_internal.
mkdirSync("dist/wasm", { recursive: true });
for (const name of ["vision_wasm_internal", "vision_wasm_nosimd_internal"])
  for (const ext of [".js", ".wasm"]) cpSync(`node_modules/@mediapipe/tasks-vision/wasm/${name}${ext}`, `dist/wasm/${name}${ext}`);
cpSync(model, "dist/models/face_landmarker.task");
