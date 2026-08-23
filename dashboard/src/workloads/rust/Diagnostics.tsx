import type { ReactElement } from "react";
export interface RustDiagnostic { file: string; line: number; column: number; code: string; message: string; }
export function Diagnostics({ diagnostics }: { diagnostics: readonly RustDiagnostic[] }): ReactElement { return <section aria-label="Rust diagnostics"><ul>{diagnostics.map((item, index) => <li key={`${item.file}:${item.line}:${index}`}>{item.file}:{item.line}:{item.column} {item.code}: {item.message}</li>)}</ul></section>; }
