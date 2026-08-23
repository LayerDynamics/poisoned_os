import type { ReactElement } from "react";
export interface FileEntry { path: string; size: number; directory: boolean; sha256?: string; }
export function sortFileEntries(entries: readonly FileEntry[]): FileEntry[] { return [...entries].sort((a, b) => a.path.localeCompare(b.path)); }
export function FileBrowser({ entries, onSelect }: { entries: readonly FileEntry[]; onSelect: (path: string) => void }): ReactElement { return <ul aria-label="Files">{sortFileEntries(entries).map((entry) => <li key={entry.path}><button type="button" onClick={() => onSelect(entry.path)}>{entry.path}</button></li>)}</ul>; }
