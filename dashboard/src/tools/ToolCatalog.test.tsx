import { describe, expect, it } from "vitest";
import { canRunTool, type ToolCatalogEntry } from "./ToolCatalog";

const entry = (status: ToolCatalogEntry["status"]): ToolCatalogEntry => ({
  id: "nfc.read",
  family: "nfc",
  status,
  purpose: "Read a test tag.",
  capabilities: ["nfc.read"],
  sample: "Present a test tag.",
});

describe("ToolCatalog", () => {
  it("offers execution only after adapter verification", () => {
    expect(canRunTool(entry("foundation"))).toBe(false);
    expect(canRunTool(entry("unavailable"))).toBe(false);
    expect(canRunTool(entry("verified"))).toBe(true);
  });

  it("rejects a verified entry without a declared capability", () => {
    expect(canRunTool({ ...entry("verified"), capabilities: [] })).toBe(false);
  });
});
