import type { ReactElement } from "react";

export function CodeEditor({ path, source, onChange }: { path: string; source: string; onChange: (source: string) => void }): ReactElement {
  return <label>{path}<textarea aria-label={`Source for ${path}`} value={source} onChange={(event) => onChange(event.target.value)} spellCheck={false} /></label>;
}
