import { describe, expect, it } from "vitest";
import { appendToolRunEvent, infraredCommandForRequest, subGhzCommandForRequest } from "./ToolRunView";

describe("ToolRunView", () => {
  it("rejects gaps and preserves ordered events", () => {
    const first = { sequence: 0n, kind: "log" as const, message: "start" };
    expect(appendToolRunEvent([], first)).toHaveLength(1);
    expect(() => appendToolRunEvent([first], { ...first, sequence: 2n })).toThrow();
  });

  it("dispatches replay only with the exact confirmation carried to firmware", () => {
    expect(infraredCommandForRequest({
      operation: "transmit",
      timeoutMs: 5000,
      maximumTimings: 1024,
      exactConfirmation: true,
    })).toEqual({
      commandId: "infrared.transmit",
      payload: { exact_confirmation: true },
    });
  });

  it("dispatches Sub-GHz analysis and replay through distinct commands", () => {
    expect(subGhzCommandForRequest({
      operation: "analyze",
      frequencyHz: 433_920_000,
      timeoutMs: 5000,
      maximumTimings: 1024,
      exactConfirmation: false,
    })).toEqual({
      commandId: "sub-ghz.analyze",
      payload: { frequency_hz: 433_920_000, timeout_ms: 5000, maximum_timings: 1024 },
    });
    expect(subGhzCommandForRequest({
      operation: "transmit",
      frequencyHz: 433_920_000,
      timeoutMs: 5000,
      maximumTimings: 1024,
      exactConfirmation: true,
    })).toEqual({
      commandId: "sub-ghz.transmit",
      payload: { exact_confirmation: true },
    });
  });
});
