import { describe, expect, it } from "vitest";
import { validateRustProject } from "./RustProjectStore";
describe("Rust project store", () => { it("rejects traversal", () => { expect(() => validateRustProject({ projectId: "rust", revision: 1, target: "wasm", cargoLockDigest: "0".repeat(64), files: { "../Cargo.toml": "bad" } })).toThrow(); }); });
