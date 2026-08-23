import { describe, expect, it } from "vitest";
import { verifyRustProvenance } from "./ProvenanceView";
describe("Rust provenance", () => { it("rejects malformed digests", () => { expect(verifyRustProvenance({ sourceDigest: "0", toolchainDigest: "0".repeat(64), sdkApi: 1, target: "wasm", outputDigest: "0".repeat(64) })).toBe(false); }); });
