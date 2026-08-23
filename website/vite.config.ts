import { resolve } from "node:path";
import { defineConfig } from "vite";

export default defineConfig({
  root: ".",
  build: {
    target: "es2022",
    rollupOptions: {
      input: {
        home: resolve(import.meta.dirname, "index.html"),
        docs: resolve(import.meta.dirname, "docs/index.html"),
      },
    },
  },
  server: {
    host: "127.0.0.1",
    fs: { allow: [resolve(import.meta.dirname, ".."), resolve(import.meta.dirname)] },
  },
});
