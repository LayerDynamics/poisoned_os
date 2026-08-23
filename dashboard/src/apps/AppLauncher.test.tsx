import { describe, expect, it } from "vitest";
import { canLaunch } from "./AppLauncher";
describe("AppLauncher", () => { it("denies locked or incomplete applications", () => { expect(canLaunch({ name: "safe", version: "1", capabilities: [], locked: false })).toBe(true); expect(canLaunch({ name: "safe", version: "1", capabilities: [], locked: true })).toBe(false); }); });
