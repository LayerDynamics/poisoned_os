import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";
import {
  InfraredTool,
  canExecuteInfraredOperation,
  validateInfraredRequest,
  type InfraredCapabilities,
} from "./InfraredTool";

const capabilities: InfraredCapabilities = { receive: true, transmit: false };

describe("InfraredTool", () => {
  it("requires independent transmit capability and exact confirmation", () => {
    expect(canExecuteInfraredOperation("receive", capabilities, false)).toBe(true);
    expect(canExecuteInfraredOperation("transmit", { ...capabilities, transmit: true }, false)).toBe(false);
    expect(canExecuteInfraredOperation("transmit", { ...capabilities, transmit: true }, true)).toBe(true);
  });

  it("bounds wait and raw timing count", () => {
    expect(validateInfraredRequest({ operation: "receive", timeoutMs: 5000, maximumTimings: 1024, exactConfirmation: false })).toEqual([]);
    expect(validateInfraredRequest({ operation: "receive", timeoutMs: 0, maximumTimings: 1024, exactConfirmation: false })).toContain("Timeout must be between 1 and 60000 milliseconds.");
    expect(validateInfraredRequest({ operation: "receive", timeoutMs: 5000, maximumTimings: 1025, exactConfirmation: false })).toContain("Raw captures are limited to 1024 timings.");
  });

  it("renders fidelity and stop controls", () => {
    const markup = renderToStaticMarkup(
      <InfraredTool capabilities={capabilities} active={true} busy={false} onRun={() => undefined} onStop={() => undefined} />,
    );
    expect(markup).toContain('aria-label="Infrared tool"');
    expect(markup).toContain("Complete raw timing evidence");
    expect(markup).toContain("Stop and release infrared hardware");
    expect(markup).toContain('<button type="submit">Run infrared operation</button>');
  });
});
