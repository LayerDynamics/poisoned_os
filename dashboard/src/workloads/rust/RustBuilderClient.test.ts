import { describe, expect, it } from "vitest";
import { validateNativeArtifactManifest } from "./RustBuilderClient";

const manifest = () => ({ target: "thumbv7em-none-eabihf", api_version: 1, abi_version: 1, entry: "poison_rust_entry", imports: ["poison_storage_open"], relocations: [0, 2], capabilities: ["storage.project.read"], digest: "a".repeat(64) });

describe("RustBuilderClient", () => {
  it("matches native admission fields", () => expect(() => validateNativeArtifactManifest(manifest())).not.toThrow());
  it("rejects host imports and unsupported relocations", () => {
    const invalid = { ...manifest(), imports: ["host_exec"], relocations: [7] };
    expect(() => validateNativeArtifactManifest(invalid)).toThrow();
  });
});
