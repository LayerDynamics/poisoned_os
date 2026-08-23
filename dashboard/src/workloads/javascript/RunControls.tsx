import type { ReactElement } from "react";

export function RunControls({ state, onRun, onStop }: { state: string; onRun: () => void; onStop: () => void }): ReactElement {
  const active = state === "starting" || state === "running" || state === "cancelling";
  return <div aria-label="JavaScript run controls"><button type="button" onClick={onRun} disabled={active}>Run</button><button type="button" onClick={onStop} disabled={state !== "starting" && state !== "running"}>Stop</button><span role="status">{state}</span></div>;
}
