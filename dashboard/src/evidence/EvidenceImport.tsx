import type { ReactElement } from "react";
export interface ImportReport { accepted: boolean; errors: readonly string[]; }
export function EvidenceImport({ report }: { report: ImportReport }): ReactElement { return <section aria-label="Evidence import"><p>{report.accepted ? "Verified and ready to import" : "Quarantined"}</p><ul>{report.errors.map((error) => <li key={error}>{error}</li>)}</ul></section>; }
