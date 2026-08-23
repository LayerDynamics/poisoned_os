import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";
import {
  LfRfidTool,
  canExecuteLfRfidOperation,
  validateLfRfidRequest,
  type LfRfidCapabilities,
} from "./LfRfidTool";

const capabilities: LfRfidCapabilities = { read: true, write: false, emulate: false };

describe("LfRfidTool", () => {
  it("requires independent mutation capability and exact confirmation", () => {
    expect(canExecuteLfRfidOperation("read", capabilities, false)).toBe(true);
    expect(canExecuteLfRfidOperation("write", { ...capabilities, write: true }, false)).toBe(false);
    expect(canExecuteLfRfidOperation("write", { ...capabilities, write: true }, true)).toBe(true);
    expect(canExecuteLfRfidOperation("emulate", capabilities, true)).toBe(false);
  });

  it("bounds the device wait before dispatch", () => {
    expect(validateLfRfidRequest({ operation: "read", timeoutMs: 5000 })).toEqual([]);
    expect(validateLfRfidRequest({ operation: "read", timeoutMs: 60_001 })).toContain("Timeout must be between 1 and 60000 milliseconds.");
  });

  it("renders the stop path and credential-redaction notice", () => {
    const markup = renderToStaticMarkup(
      <LfRfidTool capabilities={capabilities} active={true} busy={false} onRun={() => undefined} onStop={() => undefined} />,
    );
    expect(markup).toContain('aria-label="LF RFID tool"');
    expect(markup).toContain("Credential bytes are retained only in evidence storage");
    expect(markup).toContain("Stop and release LF RFID hardware");
  });
});
