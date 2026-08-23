import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";
import {
  NfcTool,
  canExecuteNfcOperation,
  validateNfcRequest,
  type NfcCapabilities,
} from "./NfcTool";

const capabilities: NfcCapabilities = {
  read: true,
  rawCapture: true,
  write: false,
  emulate: false,
};

describe("NfcTool", () => {
  it("keeps read, raw capture, write, and emulation capabilities independent", () => {
    expect(canExecuteNfcOperation("detect", capabilities, false)).toBe(true);
    expect(canExecuteNfcOperation("raw-capture", capabilities, false)).toBe(true);
    expect(canExecuteNfcOperation("write", capabilities, true)).toBe(false);
    expect(canExecuteNfcOperation("emulate", capabilities, true)).toBe(false);
    expect(canExecuteNfcOperation("write", { ...capabilities, write: true }, false)).toBe(false);
    expect(canExecuteNfcOperation("write", { ...capabilities, write: true }, true)).toBe(true);
  });

  it("bounds timeout and capture size before dispatch", () => {
    expect(validateNfcRequest({ operation: "detect", timeoutMs: 5000, maximumCaptureBytes: 0 })).toEqual([]);
    expect(validateNfcRequest({ operation: "detect", timeoutMs: 0, maximumCaptureBytes: 0 })).toContain("Timeout must be between 1 and 60000 milliseconds.");
    expect(validateNfcRequest({ operation: "raw-capture", timeoutMs: 5000, maximumCaptureBytes: 8193 })).toContain("Raw capture is limited to 8192 bytes.");
  });

  it("renders an accessible device-control surface with a visible stop path", () => {
    const markup = renderToStaticMarkup(
      <NfcTool capabilities={capabilities} active={true} busy={false} onRun={() => undefined} onStop={() => undefined} />,
    );
    expect(markup).toContain('aria-label="NFC tool"');
    expect(markup).toContain("Stop and release NFC hardware");
    expect(markup).toContain("Exact target confirmation");
  });
});
