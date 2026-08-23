import type { ReactElement } from "react";
export interface CaseSummary { caseId: string; name: string; state: "open" | "closed"; }
export function CaseWorkspace({ cases, onSelect }: { cases: readonly CaseSummary[]; onSelect: (caseId: string) => void }): ReactElement { return <section aria-label="Cases"><h2>Cases</h2><ul>{cases.map((item) => <li key={item.caseId}><button type="button" disabled={item.state === "closed"} onClick={() => onSelect(item.caseId)}>{item.name}</button></li>)}</ul></section>; }
