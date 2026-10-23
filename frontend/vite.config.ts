import react from "@vitejs/plugin-react";
import { defineConfig } from "vitest/config";

// The dev server proxies /api to the local findit_server, so the browser talks to one origin
// and the API needs no CORS. changeOrigin rewrites the Host header to 127.0.0.1:8080, which the
// server requires (it rejects other Host values to block DNS-rebinding attacks).
// FINDIT_API_TARGET (shell env, dev only) points the proxy at another port if needed.
const apiTarget = process.env.FINDIT_API_TARGET ?? "http://127.0.0.1:8080";

export default defineConfig({
  plugins: [react()],
  server: {
    host: "127.0.0.1",
    port: 5173,
    strictPort: true,
    // Same-origin proxy: no CORS needed. Off explicitly so other websites can never read
    // dev-server (and therefore proxied API) responses.
    cors: false,
    proxy: {
      "/api": { target: apiTarget, changeOrigin: true },
    },
  },
  preview: {
    host: "127.0.0.1",
    port: 4173,
    strictPort: true,
    cors: false,
    proxy: {
      "/api": { target: apiTarget, changeOrigin: true },
    },
  },
  test: {
    environment: "jsdom",
    globals: true,
    setupFiles: ["./tests/setup.ts"],
    include: ["tests/**/*.test.{ts,tsx}"],
    restoreMocks: true,
  },
});
