import type { ReactElement } from "react";

export function ProjectExplorer({ files, selected, onSelect }: { files: readonly string[]; selected: string; onSelect: (path: string) => void }): ReactElement {
  return <nav aria-label="JavaScript project files"><ul>{[...files].sort().map((path) => <li key={path}><button type="button" aria-current={path === selected} onClick={() => onSelect(path)}>{path}</button></li>)}</ul></nav>;
}
