import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";
import {
  SubGhzTool,
  canExecuteSubGhzOperation,
  validateSubGhzRequest,
  type SubGhzCapabilities,
} from "./SubGhzTool";

const capabilities: SubGhzCapabilities = {
  receive: true,
  analyze: true,
  transmit: false,
};

describe("SubGhzTool", () => {
  it("requires independent replay authority and exact confirmation", () => {
    expect(canExecuteSubGhzOperation("receive", capabilities, false)).toBe(true);
    expect(canExecuteSubGhzOperation("analyze", capabilities, false)).toBe(true);
    expect(canExecuteSubGhzOperation("transmit", { ...capabilities, transmit: true }, false)).toBe(false);
    expect(canExecuteSubGhzOperation("transmit", { ...capabilities, transmit: true }, true)).toBe(true);
  });

  it("bounds frequency, timeout, and raw timing count", () => {
    expect(validateSubGhzRequest({ operation: "receive", frequencyHz: 433_920_000, timeoutMs: 5000, maximumTimings: 1024, exactConfirmation: false })).toEqual([]);
    expect(validateSubGhzRequest({ operation: "receive", frequencyHz: 0, timeoutMs: 5000, maximumTimings: 1024, exactConfirmation: false })).toContain("Frequency must be between 1 Hz and 1 GHz; firmware applies hardware and live region policy.");
    expect(validateSubGhzRequest({ operation: "receive", frequencyHz: 433_920_000, timeoutMs: 60_001, maximumTimings: 1024, exactConfirmation: false })).toContain("Timeout must be between 1 and 60000 milliseconds.");
    expect(validateSubGhzRequest({ operation: "receive", frequencyHz: 433_920_000, timeoutMs: 5000, maximumTimings: 1025, exactConfirmation: false })).toContain("Raw captures are limited to 1024 timings.");
  });

  it("renders policy, evidence, and deterministic stop controls", () => {
    const markup = renderToStaticMarkup(
      <SubGhzTool capabilities={capabilities} active={true} busy={false} onRun={() => undefined} onStop={() => undefined} />,
    );
    expect(markup).toContain('aria-label="Sub-GHz tool"');
    expect(markup).toContain("live device region and profile policy");
    expect(markup).toContain("Raw timings are preserved as evidence");
    expect(markup).toContain("Stop and release Sub-GHz radio");
    expect(markup).toContain('<button type="submit">Run Sub-GHz operation</button>');
  });
});
