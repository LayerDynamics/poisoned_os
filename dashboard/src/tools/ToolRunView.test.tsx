import { describe, expect, it } from "vitest";
import { appendToolRunEvent } from "./ToolRunView";

describe("ToolRunView", () => {
  it("rejects gaps and preserves ordered events", () => {
    const first = { sequence: 0n, kind: "log" as const, message: "start" };
    expect(appendToolRunEvent([], first)).toHaveLength(1);
    expect(() => appendToolRunEvent([first], { ...first, sequence: 2n })).toThrow();
  });
});
