import type { ReactElement } from "react";
import type { SyntaxDiagnostic } from "./SyntaxValidator";

export function Diagnostics({ diagnostics }: { diagnostics: readonly SyntaxDiagnostic[] }): ReactElement { return <section aria-label="JavaScript diagnostics"><h3>Diagnostics</h3>{diagnostics.length === 0 ? <p role="status">No diagnostics</p> : <ul>{diagnostics.map((diagnostic, index) => <li key={`${diagnostic.file}:${diagnostic.line}:${index}`}>{diagnostic.file}:{diagnostic.line}:{diagnostic.column} {diagnostic.code}: {diagnostic.message}</li>)}</ul>}</section>; }
