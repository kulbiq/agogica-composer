// Copies MediaPipe's WASM runtime and the models next to the built page, so the tracker
// works offline. The models are downloaded once into models/ (not committed; Apache-2.0,
// https://storage.googleapis.com/mediapipe-models/).
import { cpSync, existsSync, mkdirSync, writeFileSync } from "node:fs";

const models = {
  "face_landmarker.task": "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task",
  "pose_landmarker_lite.task": "https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_lite/float16/1/pose_landmarker_lite.task",
};

mkdirSync("models", { recursive: true });
for (const [name, url] of Object.entries(models)) {
  if (existsSync(`models/${name}`)) continue;
  const response = await fetch(url);
  if (!response.ok) throw new Error(`${name} download failed: ${response.status}`);
  writeFileSync(`models/${name}`, Buffer.from(await response.arrayBuffer()));
}

// forVisionTasks() loads vision_wasm_internal (SIMD) or vision_wasm_nosimd_internal.
mkdirSync("dist/wasm", { recursive: true });
for (const name of ["vision_wasm_internal", "vision_wasm_nosimd_internal"])
  for (const ext of [".js", ".wasm"]) cpSync(`node_modules/@mediapipe/tasks-vision/wasm/${name}${ext}`, `dist/wasm/${name}${ext}`);
for (const name of Object.keys(models)) cpSync(`models/${name}`, `dist/models/${name}`);
