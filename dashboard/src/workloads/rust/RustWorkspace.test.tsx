import { describe, expect, it } from "vitest";
import { canonicalRustProject } from "./RustProjectStore";
describe("Rust workspace", () => { it("canonicalizes files independent of insertion order", () => { const base = { projectId: "rust", revision: 1, target: "native-fap" as const, cargoLockDigest: "0".repeat(64) }; expect(canonicalRustProject({ ...base, files: { "src/lib.rs": "x", "Cargo.toml": "y" } })).toBe(canonicalRustProject({ ...base, files: { "Cargo.toml": "y", "src/lib.rs": "x" } })); }); });
