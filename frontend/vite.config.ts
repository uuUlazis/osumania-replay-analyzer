import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// The C++ host serves the built assets; base "./" keeps asset URLs relative
// so they work from http://127.0.0.1:<port>/.
export default defineConfig({
  base: "./",
  plugins: [react()],
  server: {
    port: 5183,
    proxy: {
      "/api": { target: "http://127.0.0.1:24064", changeOrigin: true },
    },
  },
  build: {
    outDir: "dist",
    sourcemap: false,
    chunkSizeWarningLimit: 1500,
  },
});
