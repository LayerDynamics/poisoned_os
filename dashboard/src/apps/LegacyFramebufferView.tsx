import type { ReactElement } from "react";

export function LegacyFramebufferView({ frame, onInput }: { frame: Uint8Array | null; onInput?: (input: string) => void }): ReactElement {
  return <section aria-label="Legacy framebuffer application">
    <h2>Legacy application</h2>
    <p>{frame ? `Frame received (${frame.byteLength} bytes)` : "Waiting for frame"}</p>
    <button type="button" onClick={() => onInput?.("ok")} disabled={!onInput}>OK</button>
  </section>;
}
