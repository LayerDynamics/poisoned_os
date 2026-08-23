import type { ReactElement } from "react";
export type ConflictChoice = "replace" | "keep-both" | "cancel";
export function FileConflictDialog({ path, onChoice }: { path: string; onChoice: (choice: ConflictChoice) => void }): ReactElement { return <dialog open aria-label="File conflict"><p>{path} already exists.</p><button type="button" onClick={() => onChoice("replace")}>Replace</button><button type="button" onClick={() => onChoice("keep-both")}>Keep both</button><button type="button" onClick={() => onChoice("cancel")}>Cancel</button></dialog>; }
