import type { ReactElement } from "react";
import type { RustTarget } from "./RustProjectStore";
export function TargetSelector({ target, onChange }: { target: RustTarget; onChange: (target: RustTarget) => void }): ReactElement { return <label>Target<select value={target} onChange={(event) => onChange(event.target.value as RustTarget)}><option value="native-fap">Native FAP</option><option value="wasm">Sandboxed Wasm</option></select></label>; }
