import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// The built app is served from nginx's document root in production, where
// "/_proxy/*" already reaches kingdom-proxy-api (see nginx.baseline.conf).
// `npm run dev` here runs its own dev server instead, so this proxies the
// same path to a running docker-compose stack for a matching experience.
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      "/_proxy": {
        target: "http://localhost:4800",
        changeOrigin: true,
      },
    },
  },
});
