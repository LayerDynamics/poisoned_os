import type { ReactElement } from "react";
export interface EvidenceSummary { evidenceId: string; caseId: string; sha256: string; length: number; mediaType: string; derived: boolean; }
export function validateEvidenceSummary(record: EvidenceSummary): void { if (!record.evidenceId || !record.caseId || !/^[0-9a-f]{64}$/.test(record.sha256) || record.length < 0) throw new Error("invalid evidence summary"); }
export function EvidenceInspector({ record }: { record: EvidenceSummary }): ReactElement { validateEvidenceSummary(record); return <article aria-label="Evidence"><h2>{record.evidenceId}</h2><p>{record.mediaType} · {record.length} bytes · {record.derived ? "derived" : "raw"}</p><code>{record.sha256}</code></article>; }
