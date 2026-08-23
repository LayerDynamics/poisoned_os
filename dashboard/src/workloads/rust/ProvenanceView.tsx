import type { ReactElement } from "react";
export interface RustProvenance { sourceDigest: string; toolchainDigest: string; sdkApi: number; target: string; outputDigest: string; }
export function verifyRustProvenance(value: RustProvenance): boolean { return [value.sourceDigest, value.toolchainDigest, value.outputDigest].every((digest) => /^[0-9a-f]{64}$/.test(digest)) && value.sdkApi > 0 && value.target.length > 0; }
export function ProvenanceView({ provenance }: { provenance: RustProvenance }): ReactElement { return <dl aria-label="Rust provenance"><dt>Target</dt><dd>{provenance.target}</dd><dt>SDK API</dt><dd>{provenance.sdkApi}</dd><dt>Verified</dt><dd>{verifyRustProvenance(provenance) ? "yes" : "no"}</dd></dl>; }
