import type { ReactElement } from "react";
export function CargoEditor({ source, onChange }: { source: string; onChange: (source: string) => void }): ReactElement { return <label>Cargo.toml<textarea aria-label="Cargo.toml" value={source} onChange={(event) => onChange(event.target.value)} spellCheck={false} /></label>; }
