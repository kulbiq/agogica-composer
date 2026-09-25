import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// base './' so every asset resolves under JUCE's resource-provider root.
// Port is fixed: Debug builds with COMPOSER_UI_DEV_SERVER load http://localhost:5173/.
export default defineConfig({
  base: "./",
  plugins: [react()],
  server: { port: 5173, strictPort: true },
});
