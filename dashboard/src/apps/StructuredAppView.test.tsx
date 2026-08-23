import { describe, expect, it } from "vitest";
import { appendAppEvent, type AppEvent } from "./AppEvents";

describe("StructuredAppView contract", () => {
  it("requires contiguous events before rendering", () => {
    const first: AppEvent = { runId: "run", sequence: 0n, timeMs: 1, level: "info", kind: "status", message: "ready", artifactIds: [] };
    const second = { ...first, sequence: 1n, message: "done" };
    expect(appendAppEvent(appendAppEvent([], first), second)).toHaveLength(2);
  });
});
