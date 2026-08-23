import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";
import { sha256Hex } from "../../files/FileTransferQueue";
import {
  DependencyManager,
  importDependencyBundle,
  validateJavaScriptLock,
} from "./DependencyManager";

const encoder = new TextEncoder();

function base64(bytes: Uint8Array): string {
  return btoa(String.fromCharCode(...bytes));
}

async function fixture(content = encoder.encode("module.exports = 7;\n")) {
  const path = "index.js";
  const canonical = new Uint8Array(encoder.encode(path).byteLength + 1 + content.byteLength);
  canonical.set(encoder.encode(path));
  canonical.set(content, encoder.encode(path).byteLength + 1);
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", canonical));
  const lock = {
    schema: "poison.javascript.lock/v1",
    runtime: "poison-mjs-1",
    entrypoint: "src/main.js",
    dependencies: [{
      name: "tiny-value",
      version: "1.0.0",
      main: path,
      integrity: `sha256-${base64(digest)}`,
      source: "bundled",
      license: "MIT",
      runtime: "poison-mjs-1",
      dependencies: [],
      files: [{ path, sha256: await sha256Hex(content), bytes: content.byteLength }],
    }],
  };
  return { lock, content };
}

describe("offline JavaScript dependency manager", () => {
  it("imports only files bound by the lock and immutable package digest", async () => {
    const { lock, content } = await fixture();
    const lockSource = JSON.stringify(lock);
    const imported = await importDependencyBundle(lockSource, [{
      path: "vendor/tiny-value/1.0.0/index.js",
      data: content,
    }]);
    expect(imported["poison-js.lock"]).toBe(`${JSON.stringify(lock, null, 2)}\n`);
    expect(imported["vendor/tiny-value/1.0.0/index.js"]).toBe("module.exports = 7;\n");
  });

  it("rejects tampering and unknown dependency graph edges", async () => {
    const { lock, content } = await fixture();
    await expect(importDependencyBundle(JSON.stringify(lock), [{
      path: "vendor/tiny-value/1.0.0/index.js",
      data: encoder.encode("module.exports = 8;\n"),
    }])).rejects.toThrow("digest mismatch");
    const invalid = structuredClone(lock) as unknown as { dependencies: Array<{ dependencies: string[] }> };
    invalid.dependencies[0].dependencies = ["missing"];
    expect(() => validateJavaScriptLock(JSON.stringify(invalid))).toThrow("unknown dependency");
    expect(content.byteLength).toBeGreaterThan(0);
  });

  it("rejects dependency identity that redirects the vendor root", async () => {
    const { lock } = await fixture();
    const invalid = structuredClone(lock) as unknown as { dependencies: Array<{ name: string }> };
    invalid.dependencies[0].name = "..";
    expect(() => validateJavaScriptLock(JSON.stringify(invalid))).toThrow("identity");
  });

  it("rejects a dependency main outside its immutable inventory", async () => {
    const { lock } = await fixture();
    const invalid = structuredClone(lock) as unknown as { dependencies: Array<{ main: string }> };
    invalid.dependencies[0].main = "missing.js";
    expect(() => validateJavaScriptLock(JSON.stringify(invalid))).toThrow("main");
  });

  it("renders an offline folder import with visible dependency identity", async () => {
    const { lock } = await fixture();
    const markup = renderToStaticMarkup(<DependencyManager
      lockSource={`${JSON.stringify(lock)}\n`}
      onImport={() => {}}
    />);
    expect(markup).toContain("Offline dependencies");
    expect(markup).toContain("tiny-value");
    expect(markup).toContain("Select dependency folder");
  });
});
