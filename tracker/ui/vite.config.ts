import { defineConfig } from "vite";

// base './' so every asset resolves under JUCE's resource-provider root.
export default defineConfig({
  base: "./",
  server: { port: 5174, strictPort: true, fs: { allow: ["..", "../../ui"] } },
});
