import { defineConfig } from "astro/config";
import { unified } from "@astrojs/markdown-remark";
import repositoryLinks from "./src/lib/repository-links.mjs";

export default defineConfig({
  site: "https://poisoned-os.pages.dev",
  trailingSlash: "always",
  markdown: {
    processor: unified({
      remarkPlugins: [repositoryLinks],
    }),
  },
  build: {
    format: "directory",
  },
});
