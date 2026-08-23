import { useState, type ReactElement } from "react";
import { BuildPanel, type RustBuildState } from "./BuildPanel";
import { CargoEditor } from "./CargoEditor";
import { Diagnostics, type RustDiagnostic } from "./Diagnostics";
import { ProjectImport, normalizeRustImport } from "./ProjectImport";
import { ProvenanceView, type RustProvenance } from "./ProvenanceView";
import { TargetSelector } from "./TargetSelector";
import type { RustTarget } from "./RustProjectStore";
import { RustBuilderClient, type NativeArtifactManifest, validateNativeArtifactManifest } from "./RustBuilderClient";

export function RustWorkspace({ initialFiles = { "Cargo.toml": "[package]\nname = \"project\"" }, builderClient, nativeArtifact }: { initialFiles?: Readonly<Record<string, string>>; builderClient?: RustBuilderClient; nativeArtifact?: NativeArtifactManifest }): ReactElement {
  const [files, setFiles] = useState<Record<string, string>>({ ...initialFiles });
  const [target, setTarget] = useState<RustTarget>("native-fap");
  const [state, setState] = useState<RustBuildState>("idle");
  const diagnostics: RustDiagnostic[] = files["Cargo.toml"] ? [] : [{ file: "Cargo.toml", line: 1, column: 1, code: "CARGO_MISSING", message: "Cargo.toml is required" }];
  const provenance: RustProvenance = { sourceDigest: "0".repeat(64), toolchainDigest: "0".repeat(64), sdkApi: 1, target, outputDigest: "0".repeat(64) };
  const startBuild = () => {
    setState("queued");
    if (diagnostics.length > 0 || target !== "native-fap" || !builderClient || !nativeArtifact) { setState("failed"); return; }
    setState("building");
    try { validateNativeArtifactManifest(nativeArtifact); } catch { setState("failed"); return; }
    void builderClient.validateNativeArtifact(nativeArtifact).then(() => setState("succeeded"), () => setState("failed"));
  };
  return <section aria-label="Rust workspace"><h2>Rust workspace</h2><TargetSelector target={target} onChange={setTarget} /><CargoEditor source={files["Cargo.toml"] ?? ""} onChange={(source) => setFiles((current) => ({ ...current, "Cargo.toml": source }))} /><BuildPanel state={state} onBuild={startBuild} onCancel={() => setState("cancelled")} /><ProjectImport onImport={(imported) => setFiles(normalizeRustImport(imported))} /><Diagnostics diagnostics={diagnostics} /><ProvenanceView provenance={provenance} /></section>;
}
