import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";
import {
  IButtonTool,
  canExecuteIButtonOperation,
  validateIButtonRequest,
  type IButtonCapabilities,
} from "./IButtonTool";

const capabilities: IButtonCapabilities = { read: true, write: false, emulate: false };

describe("IButtonTool", () => {
  it("keeps read, write, and emulation authorization independent", () => {
    expect(canExecuteIButtonOperation("read", capabilities, false)).toBe(true);
    expect(canExecuteIButtonOperation("write", { ...capabilities, write: true }, false)).toBe(false);
    expect(canExecuteIButtonOperation("write", { ...capabilities, write: true }, true)).toBe(true);
    expect(canExecuteIButtonOperation("emulate", capabilities, true)).toBe(false);
  });

  it("rejects unbounded device waits", () => {
    expect(validateIButtonRequest({ operation: "read", timeoutMs: 5000 })).toEqual([]);
    expect(validateIButtonRequest({ operation: "read", timeoutMs: 0 })).toContain("Timeout must be between 1 and 60000 milliseconds.");
  });

  it("renders electrical consequences, redaction, and a stop path", () => {
    const markup = renderToStaticMarkup(
      <IButtonTool capabilities={capabilities} active={true} busy={false} onRun={() => undefined} onStop={() => undefined} />,
    );
    expect(markup).toContain('aria-label="iButton and 1-Wire tool"');
    expect(markup).toContain("may power the 1-Wire bus");
    expect(markup).toContain("Key bytes remain in evidence storage");
    expect(markup).toContain("Stop and release 1-Wire hardware");
  });
});
