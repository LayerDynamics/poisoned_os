export const SUPPORT_BUNDLE_SCHEMA = "poison.support-bundle/v1" as const;

export interface SupportBundleCounters {
  sessionEstablished: number;
  transportErrors: number;
  droppedFrames: number;
  retriedFrames: number;
  commandFailures: number;
  appCrashes: number;
  policyDenials: number;
  packageVerifications: number;
  packageRevocations: number;
  updateStages: number;
  updateHealth: number;
  updateRollbacks: number;
  recoveries: number;
  javascriptStarts: number;
  javascriptTerminals: number;
  javascriptCrashes: number;
  javascriptLimits: number;
  javascriptRecoveries: number;
}

export interface SupportBundleEvent {
  eventId: number;
  category: string;
  summary: string;
  timestampMs: number;
  correlationDigest: string;
}

export interface SupportBundle {
  schema: typeof SUPPORT_BUNDLE_SCHEMA;
  consent: { previewed: true; acceptedAtMs: number };
  components: readonly { name: string; version: string }[];
  counters: SupportBundleCounters;
  events: readonly SupportBundleEvent[];
  files: readonly { path: `/ext/${string}`; sha256: string; size: number }[];
}

export interface DiagnosticSnapshotClient {
  requestStream(request: Main, maxResponses?: number, signal?: AbortSignal): Promise<readonly Main[]>;
}

export interface DiagnosticSnapshot {
  counters: SupportBundleCounters;
  events: readonly SupportBundleEvent[];
}

function bytesToHex(value: Uint8Array): string {
  return Array.from(value, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

export async function requestDiagnosticSnapshot(
  session: DiagnosticSnapshotClient,
  commandId: number,
  afterEventId = 0n,
  maxEvents = 16,
  signal?: AbortSignal,
): Promise<DiagnosticSnapshot> {
  if (!Number.isInteger(commandId) || commandId < 1 || commandId > 0xffffffff ||
      afterEventId < 0n || !Number.isInteger(maxEvents) || maxEvents < 1 || maxEvents > 16) {
    throw new Error("diagnostic snapshot request is outside its bounds");
  }
  const request = create(MainSchema, {
    commandId,
    content: {
      case: "poisonDiagnosticSnapshotRequest",
      value: create(DiagnosticSnapshotRequestSchema, { afterEventId, maxEvents }),
    },
  });
  const responses = await session.requestStream(request, maxEvents + 1, signal);
  if (responses.length === 0 || responses[0].content.case !== "poisonDiagnosticCounters") {
    throw new Error("device omitted diagnostic counters");
  }
  const counters = responses[0].content.value;
  const events: SupportBundleEvent[] = [];
  let previousEventId = afterEventId;
  for (const response of responses.slice(1)) {
    if (response.content.case !== "poisonDiagnosticEvent") throw new Error("device returned an invalid diagnostic stream");
    const event = response.content.value;
    if (event.eventId <= previousEventId || event.eventId > BigInt(Number.MAX_SAFE_INTEGER) ||
        !event.category || event.category.length > 24 || !event.summary || event.summary.length > 96 ||
        event.correlationDigest.byteLength !== 32) {
      throw new Error("device returned an invalid diagnostic event");
    }
    events.push({
      eventId: Number(event.eventId),
      category: event.category,
      summary: event.summary,
      timestampMs: Number(event.timestampMs),
      correlationDigest: bytesToHex(event.correlationDigest),
    });
    previousEventId = event.eventId;
  }
  return { counters: { ...counters }, events };
}

export function createSupportBundle(input: Omit<SupportBundle, "schema" | "consent"> & { acceptedAtMs: number }): SupportBundle {
  if (!Number.isSafeInteger(input.acceptedAtMs) || input.acceptedAtMs <= 0) throw new Error("consent timestamp is required");
  if (input.components.length > 32 || input.events.length > 64 || input.files.length > 32) throw new Error("support bundle is over bounded capacity");
  for (const event of input.events) {
    if (!event.summary || event.summary.length > 96 || !/^[0-9a-f]{64}$/.test(event.correlationDigest)) throw new Error("invalid diagnostic event");
  }
  for (const file of input.files) {
    if (!file.path.startsWith("/ext/") || !/^[0-9a-f]{64}$/.test(file.sha256)) throw new Error("invalid support file reference");
  }
  return { schema: SUPPORT_BUNDLE_SCHEMA, consent: { previewed: true, acceptedAtMs: input.acceptedAtMs }, components: input.components, counters: input.counters, events: input.events, files: input.files };
}

export function SupportBundlePreview({ bundle }: { bundle: SupportBundle }): React.ReactElement {
  return <section aria-label="Support bundle preview"><h2>Support bundle preview</h2><p>{bundle.events.length} diagnostic events and {bundle.files.length} file digests</p></section>;
}

export function DeviceDiagnostics({ session }: { session: DiagnosticSnapshotClient }): ReactElement {
  const commandId = useRef(5000);
  const [snapshot, setSnapshot] = useState<DiagnosticSnapshot | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const refresh = async () => {
    setBusy(true);
    setError(null);
    try {
      setSnapshot(await requestDiagnosticSnapshot(session, commandId.current++));
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };
  return <section className="update-card" aria-labelledby="diagnostics-title">
    <div className="update-heading"><div><p className="eyebrow">AUTHENTICATED DEVICE STATE</p><h2 id="diagnostics-title">Diagnostics</h2></div><output className="update-state">{snapshot ? `${snapshot.events.length} events` : "not loaded"}</output></div>
    <button disabled={busy} onClick={() => void refresh()}>Refresh diagnostics</button>
    {snapshot && <p role="status">{snapshot.counters.packageVerifications} package checks · {snapshot.counters.updateStages} update stages · {snapshot.counters.updateRollbacks} rollbacks · {snapshot.counters.recoveries} recoveries · {snapshot.counters.javascriptStarts} JS starts · {snapshot.counters.javascriptLimits} JS limits</p>}
    {error && <p className="error" role="alert">{error}</p>}
  </section>;
}
import { create } from "@bufbuild/protobuf";
import { useRef, useState, type ReactElement } from "react";
import { MainSchema, type Main } from "../generated/flipper_pb";
import { DiagnosticSnapshotRequestSchema } from "../generated/poison_diagnostics_pb";
