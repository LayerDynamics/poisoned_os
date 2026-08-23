import { describe, expect, it } from "vitest";
import { packageJavaScriptProject } from "./PackageProject";
import { validateJavaScriptManifest } from "./manifest";

describe("JavaScript project packaging", () => {
  const manifest = validateJavaScriptManifest({ format: 1, id: "org.poisonedos.example", name: "Example", version: "1.0.0", language: "javascript", entrypoint: "main.js", runtime: "poison-mjs-1", runtimeApi: 1, firmwareApi: "1.0.0", capabilities: [], limits: { heapBytes: 1, wallTimeMs: 1, logBytes: 1, artifactBytes: 1 }, dependencies: "poison-js.lock", servedUi: null });

  it("is deterministic regardless of file insertion order", () => {
    const first = packageJavaScriptProject({ manifest, files: { "z.js": "z", "main.js": "main" } });
    const second = packageJavaScriptProject({ manifest, files: { "main.js": "main", "z.js": "z" } });
    expect(first.canonical).toBe(second.canonical);
  });

  it("rejects traversal, hidden, and non-JavaScript project members", () => {
    expect(() => packageJavaScriptProject({ manifest, files: { "main.js": "ok", "../secret.js": "no" } })).toThrow();
    expect(() => packageJavaScriptProject({ manifest, files: { "main.js": "ok", ".env.js": "no" } })).toThrow();
    expect(() => packageJavaScriptProject({ manifest, files: { "main.js": "ok", "payload.bin": "no" } })).toThrow();
  });

  it("includes the exact lock and vendored JavaScript sources", () => {
    const packaged = packageJavaScriptProject({
      manifest,
      files: {
        "main.js": "require('./vendor/tiny/1.0.0/index.js')",
        "poison-js.lock": "{}\n",
        "vendor/tiny/1.0.0/index.js": "module.exports = 7;\n",
      },
    });
    expect(packaged.files.map((file) => file.path)).toEqual([
      "main.js",
      "poison-js.lock",
      "vendor/tiny/1.0.0/index.js",
    ]);
  });
});
