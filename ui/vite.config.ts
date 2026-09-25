import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// base './' so every asset resolves under JUCE's resource-provider root.
export default defineConfig({
  base: "./",
  plugins: [react()],
});
