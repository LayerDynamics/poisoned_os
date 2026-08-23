import { describe, expect, it } from "vitest";
import { normalizeRustImport } from "./ProjectImport";
describe("Rust project import", () => { it("requires Cargo.toml and sorts files", () => { expect(Object.keys(normalizeRustImport({ "src/lib.rs": "x", "Cargo.toml": "y" }))).toEqual(["Cargo.toml", "src/lib.rs"]); }); });
