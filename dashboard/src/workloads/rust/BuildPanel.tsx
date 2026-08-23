import type { ReactElement } from "react";
export type RustBuildState = "idle" | "queued" | "building" | "succeeded" | "failed" | "cancelled";
export function BuildPanel({ state, onBuild, onCancel }: { state: RustBuildState; onBuild: () => void; onCancel: () => void }): ReactElement { return <section aria-label="Rust build"><button type="button" onClick={onBuild} disabled={state === "building"}>Build</button><button type="button" onClick={onCancel} disabled={state !== "building"}>Cancel</button><span role="status">{state}</span></section>; }
