import { defineCollection } from "astro:content";
import { glob } from "astro/loaders";

const docs = defineCollection({
  loader: glob({
    pattern: ["README.md", "docs/**/*.md"],
    base: new URL("../..", import.meta.url),
    retainBody: true,
  }),
});

export const collections = { docs };
