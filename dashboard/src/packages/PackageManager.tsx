import { create } from "@bufbuild/protobuf";
import { useCallback, useEffect, useRef, useState, type ReactElement } from "react";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import {
  PackageLifecycleState,
  PackageOperation,
  PackageOperationRequestSchema,
  type PackageOperationStatus,
} from "../generated/poison_packages_pb";
import { PackageCatalogClient, PackageSources, type PackageAvailability } from "./PackageSources";

export type PackageState = "installed" | "staged" | "verified" | "active" | "disabled" | "quarantined" | "removed";
export interface PackageRecord { id: string; version: string; signer: string; capabilityMask: bigint; state: PackageState; protectedPackage: boolean; }
export interface PackageRequestClient {
  request(request: Main, signal?: AbortSignal): Promise<Main>;
  requestStream(request: Main, maxResponses?: number, signal?: AbortSignal): Promise<readonly Main[]>;
}
export interface PackageDraft {
  packageId: string; version: string; previousVersion: string; manifestPath: string;
  candidateDigest: string; previousDigest: string; signingKeyId: string; capabilityMask: bigint;
  releaseSequence: number; contentBytes: number; previousState: PackageLifecycleState;
  protectedPackage: boolean; confirmationRequired: boolean;
}

const STATE_NAMES: Record<PackageLifecycleState, PackageState> = {
  [PackageLifecycleState.INSTALLED]: "installed", [PackageLifecycleState.STAGED]: "staged",
  [PackageLifecycleState.VERIFIED]: "verified", [PackageLifecycleState.ACTIVE]: "active",
  [PackageLifecycleState.DISABLED]: "disabled", [PackageLifecycleState.QUARANTINED]: "quarantined",
  [PackageLifecycleState.REMOVED]: "removed",
};
const CAPABILITY_NAMES = ["status", "control", "launch", "files", "evidence", "radio", "native", "destructive"] as const;
const EMPTY_DRAFT: PackageDraft = {
  packageId: "org.poisonedos.example", version: "1.0.0", previousVersion: "",
  manifestPath: "/ext/apps/.staging/org.poisonedos.example/package.poison",
  candidateDigest: "", previousDigest: "", signingKeyId: "", capabilityMask: 0n,
  releaseSequence: 1, contentBytes: 1, previousState: PackageLifecycleState.REMOVED,
  protectedPackage: false, confirmationRequired: false,
};

export function canPackageTransition(record: PackageRecord, next: PackageState): boolean {
  if (record.protectedPackage && next === "removed") return false;
  return (record.state === "installed" && next === "staged") ||
    (record.state === "staged" && ["verified", "quarantined"].includes(next)) ||
    (record.state === "verified" && next === "active") ||
    (record.state === "active" && ["disabled", "removed"].includes(next)) ||
    (record.state === "disabled" && ["active", "removed"].includes(next)) ||
    (record.state === "quarantined" && next === "removed");
}

export function createPackageRequest(commandId: number, operation: PackageOperation, draft: PackageDraft, confirmationToken = new Uint8Array(), healthy = false): Main {
  return create(MainSchema, { commandId, content: { case: "poisonPackageOperationRequest", value: create(PackageOperationRequestSchema, {
    operation, packageId: draft.packageId, version: draft.version, previousVersion: draft.previousVersion,
    manifestPath: draft.manifestPath, candidateDigest: draft.candidateDigest, previousDigest: draft.previousDigest,
    signingKeyId: draft.signingKeyId, capabilityMask: draft.capabilityMask, releaseSequence: draft.releaseSequence,
    receivedBytes: operation === PackageOperation.STAGE ? draft.contentBytes : 0, contentBytes: draft.contentBytes,
    previousState: draft.previousState, protectedPackage: draft.protectedPackage,
    confirmationRequired: draft.confirmationRequired, healthy, confirmationToken,
  }) } });
}

export function packageStatusToRecord(status: PackageOperationStatus): PackageRecord {
  return { id: status.packageId, version: status.version, signer: status.signingKeyId,
    capabilityMask: status.capabilityMask, state: STATE_NAMES[status.state], protectedPackage: status.protectedPackage };
}

function capabilitySummary(mask: bigint): string {
  const names = CAPABILITY_NAMES.filter((_, index) => (mask & (1n << BigInt(index))) !== 0n);
  return names.length === 0 ? "none" : names.join(", ");
}

export function PackageManager({ session }: { session: PackageRequestClient }): ReactElement {
  const [draft, setDraft] = useState<PackageDraft>(EMPTY_DRAFT);
  const [status, setStatus] = useState<PackageOperationStatus | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [inventory, setInventory] = useState<readonly PackageAvailability[]>([]);
  const confirmationToken = useRef(new Uint8Array());
  const commandId = useRef(2000);
  const setText = (field: keyof PackageDraft, value: string) => setDraft((current) => ({ ...current, [field]: value }));
  const refreshInventory = useCallback(async (signal?: AbortSignal) => {
    const snapshot = await new PackageCatalogClient(session).readAll(signal);
    setInventory(snapshot.packages);
  }, [session]);
  useEffect(() => {
    const controller = new AbortController();
    void refreshInventory(controller.signal).catch((reason) => {
      if (!controller.signal.aborted) setError(reason instanceof Error ? reason.message : String(reason));
    });
    return () => controller.abort();
  }, [refreshInventory]);
  const run = async (operation: PackageOperation, healthy = false) => {
    setBusy(true); setError(null);
    try {
      const response = await session.request(createPackageRequest(commandId.current++, operation, draft, confirmationToken.current, healthy));
      if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonPackageOperationStatus")
        throw new Error(`device rejected package operation ${PackageOperation[operation]}`);
      setStatus(response.content.value);
      confirmationToken.current = response.content.value.confirmationToken.slice();
      await refreshInventory();
    } catch (reason) { setError(reason instanceof Error ? reason.message : String(reason)); }
    finally { setBusy(false); }
  };
  return <section className="update-card" aria-labelledby="package-title">
    <div className="update-heading"><div><p className="eyebrow">SIGNED LOCAL PACKAGE TRANSACTION</p><h2 id="package-title">Application packages</h2></div><output className="update-state">{status ? STATE_NAMES[status.state] : "not imported"}</output></div>
    <PackageSources packages={inventory} onInstall={(record) => setDraft((current) => ({
      ...current, packageId: record.id, version: record.version, manifestPath: record.sourcePath,
      candidateDigest: record.digest, signingKeyId: record.signer, capabilityMask: record.capabilityMask,
    }))} />
    <button type="button" className="secondary" disabled={busy} onClick={() => void refreshInventory().catch((reason) => setError(reason instanceof Error ? reason.message : String(reason)))}>Refresh package inventory</button>
    <div className="update-grid">
      <label>Package ID<input value={draft.packageId} onChange={(event) => setText("packageId", event.target.value)} /></label>
      <label>Version<input value={draft.version} onChange={(event) => setText("version", event.target.value)} /></label>
      <label>Previous version<input value={draft.previousVersion} onChange={(event) => setText("previousVersion", event.target.value)} /></label>
      <label>Staged archive path<input value={draft.manifestPath} onChange={(event) => setText("manifestPath", event.target.value)} /></label>
      <label>Archive SHA-256<input value={draft.candidateDigest} onChange={(event) => setText("candidateDigest", event.target.value)} /></label>
      <label>Previous SHA-256<input value={draft.previousDigest} onChange={(event) => setText("previousDigest", event.target.value)} /></label>
      <label>Signing key ID<input value={draft.signingKeyId} onChange={(event) => setText("signingKeyId", event.target.value)} /></label>
      <label>Capability mask<input value={draft.capabilityMask.toString()} onChange={(event) => setDraft((current) => ({ ...current, capabilityMask: BigInt(event.target.value || "0") }))} /></label>
      <label>Release sequence<input type="number" min="1" value={draft.releaseSequence} onChange={(event) => setDraft((current) => ({ ...current, releaseSequence: Number(event.target.value) }))} /></label>
      <label>Archive bytes<input type="number" min="1" value={draft.contentBytes} onChange={(event) => setDraft((current) => ({ ...current, contentBytes: Number(event.target.value) }))} /></label>
    </div>
    <p>Provenance: explicitly staged local archive · signer: {status?.signingKeyId || draft.signingKeyId || "unverified"} · permissions: {capabilitySummary(status?.capabilityMask ?? draft.capabilityMask)}</p>
    <div className="update-actions">
      <button disabled={busy} onClick={() => void run(PackageOperation.IMPORT)}>Import and verify manifest</button>
      <button disabled={busy || !status} onClick={() => void run(PackageOperation.STAGE)}>Stage bytes</button>
      <button disabled={busy || !status} onClick={() => void run(PackageOperation.VERIFY)}>Verify archive payloads</button>
      <button disabled={busy || confirmationToken.current.byteLength !== 16} onClick={() => void run(PackageOperation.ACTIVATE)}>Activate on device</button>
      <button disabled={busy || !status} onClick={() => void run(PackageOperation.HEALTH, true)}>Report healthy</button>
      <button className="secondary" disabled={busy} onClick={() => void run(PackageOperation.INSPECT)}>Inspect</button>
      <button className="secondary" disabled={busy || !status} onClick={() => void run(PackageOperation.DISABLE)}>Disable</button>
      <button className="secondary" disabled={busy || !status} onClick={() => void run(PackageOperation.ENABLE)}>Enable</button>
      <button className="secondary" disabled={busy || !status || status.protectedPackage} onClick={() => void run(PackageOperation.REMOVE)}>Remove</button>
      <button className="secondary" disabled={busy || !status} onClick={() => void run(PackageOperation.ROLLBACK)}>Rollback</button>
      <button className="secondary" disabled={busy || !status} onClick={() => void run(PackageOperation.QUARANTINE)}>Quarantine</button>
    </div>
    {status && <p role="status">{status.result} · {status.receivedBytes}/{status.contentBytes} bytes · {STATE_NAMES[status.state]}</p>}
    {error && <p className="error" role="alert">{error}</p>}
  </section>;
}
