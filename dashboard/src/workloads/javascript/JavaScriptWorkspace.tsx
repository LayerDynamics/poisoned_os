import { useCallback, useEffect, useMemo, useRef, useState, type ReactElement } from "react";
import { DeviceControlClient, type DeviceControlSession } from "../../device/DeviceControlClient";
import { DeviceStatusClient, type DeviceStatusSession } from "../../device/DeviceStatus";
import { EvidenceClient } from "../../evidence/EvidenceClient";
import { WorkloadState as DeviceWorkloadState, type WorkloadStatus } from "../../generated/poison_workload_pb";
import type { TransferProgress } from "../../files/FileTransferQueue";
import { sha256Hex } from "../../files/FileTransferQueue";
import { CodeEditor } from "./CodeEditor";
import { Console, type ConsoleFrame } from "./Console";
import { DependencyManager } from "./DependencyManager";
import { Diagnostics } from "./Diagnostics";
import { ProjectExplorer } from "./ProjectExplorer";
import { RunControls } from "./RunControls";
import { validateJavaScriptSyntax, type SyntaxDiagnostic } from "./SyntaxValidator";
import { packageJavaScriptProject } from "./PackageProject";
import {
  loadProjectRevisions,
  restoreProjectRevision,
  saveProjectRevision,
  type ProjectRevision,
} from "./RevisionStore";
import type { JavaScriptManifest, WorkloadState } from "./manifest";
import { WorkloadClient, type JavaScriptDeployment, type WorkloadRpcSession } from "./WorkloadClient";
import { BundleClient } from "./served/BundleClient";
import { ServedAppHost } from "./served/ServedAppHost";
import type { BrokerCapability, BrokerRequest } from "./served/MessageBroker";

const TERMINAL_STATES = new Set<WorkloadState>([
  "completed",
  "failed",
  "cancelled",
  "timed-out",
  "crashed",
  "disconnected",
]);

const SERVED_BROKER_CAPABILITIES: readonly BrokerCapability[] = [
  "device.status.read",
  "device.app.run",
  "evidence.create",
];

function payloadObject(payload: unknown): Record<string, unknown> {
  if (typeof payload !== "object" || payload === null || Array.isArray(payload)) {
    throw new Error("served interface payload must be an object");
  }
  return payload as Record<string, unknown>;
}

export function deviceWorkloadState(state: DeviceWorkloadState): WorkloadState {
  switch (state) {
    case DeviceWorkloadState.QUEUED: return "queued";
    case DeviceWorkloadState.RUNNING: return "running";
    case DeviceWorkloadState.CANCELLING: return "cancelling";
    case DeviceWorkloadState.COMPLETED: return "completed";
    case DeviceWorkloadState.FAILED: return "failed";
    case DeviceWorkloadState.CANCELLED: return "cancelled";
    case DeviceWorkloadState.TIMED_OUT: return "timed-out";
    case DeviceWorkloadState.CRASHED: return "crashed";
    case DeviceWorkloadState.DISCONNECTED: return "disconnected";
    default: return "failed";
  }
}

function mergeConsoleFrames(current: readonly ConsoleFrame[], incoming: readonly ConsoleFrame[]): ConsoleFrame[] {
  const frames = new Map(current.map((frame) => [frame.sequence, frame]));
  for (const frame of incoming) frames.set(frame.sequence, frame);
  return [...frames.values()].sort((left, right) => left.sequence - right.sequence);
}

function delay(milliseconds: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    const timeout = window.setTimeout(resolve, milliseconds);
    signal.addEventListener("abort", () => {
      window.clearTimeout(timeout);
      reject(new DOMException("workload monitor cancelled", "AbortError"));
    }, { once: true });
  });
}

function isAbort(error: unknown): boolean {
  return error instanceof DOMException && error.name === "AbortError";
}

export interface JavaScriptWorkspaceProps {
  readonly session: WorkloadRpcSession;
  readonly manifest: JavaScriptManifest;
  readonly initialFiles: Readonly<Record<string, string>>;
  readonly onRun?: (canonicalProject: string) => void | Promise<void>;
  readonly onStop?: () => void | Promise<void>;
}

export function JavaScriptWorkspace({ session, manifest, initialFiles, onRun, onStop }: JavaScriptWorkspaceProps): ReactElement {
  const [files, setFiles] = useState<Record<string, string>>({ ...initialFiles });
  const [selected, setSelected] = useState(manifest.entrypoint);
  const [state, setState] = useState<WorkloadState>("queued");
  const [frames, setFrames] = useState<ConsoleFrame[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [transfer, setTransfer] = useState<TransferProgress | null>(null);
  const [revisions, setRevisions] = useState<ProjectRevision[]>([]);
  const [revisionMessage, setRevisionMessage] = useState("");
  const [artifactFile, setArtifactFile] = useState<File | null>(null);
  const [artifactMessage, setArtifactMessage] = useState("");
  const client = useMemo(() => new WorkloadClient(session), [session]);
  const bundleClient = useMemo(() => new BundleClient(session), [session]);
  const activeWorkload = useRef<string | null>(null);
  const activeDeployment = useRef<JavaScriptDeployment | null>(null);
  const monitorAbort = useRef<AbortController | null>(null);
  const dirty = useRef(false);
  const diagnostics: SyntaxDiagnostic[] = useMemo(() => validateJavaScriptSyntax(files, manifest.entrypoint), [files, manifest.entrypoint]);
  const loadServedBundle = useCallback(async () => {
    if (!manifest.servedUi) throw new Error("project has no served interface");
    return bundleClient.loadBundle({
      id: manifest.servedUi.bundleId,
      version: manifest.servedUi.version,
      contentSha256: manifest.servedUi.contentSha256,
    });
  }, [bundleClient, manifest.servedUi]);
  const authorizeServedOperation = useCallback(
    () => (session as WorkloadRpcSession & { readonly status?: string }).status === "active",
    [session],
  );
  const dispatchServedOperation = useCallback(async (request: BrokerRequest, signal: AbortSignal) => {
    if (request.capability === "device.status.read") {
      const statusSession = session as WorkloadRpcSession & Partial<DeviceStatusSession>;
      if (!statusSession.transportKind || !statusSession.transportHealth) {
        throw new Error("device status transport is unavailable");
      }
      return new DeviceStatusClient(statusSession as DeviceStatusSession).read(signal);
    }
    if (request.capability === "device.app.run") {
      const payload = payloadObject(request.payload);
      if (typeof payload.name !== "string" ||
          (payload.args !== undefined && typeof payload.args !== "string") ||
          typeof (session as Partial<DeviceControlSession>).onNotification !== "function") {
        throw new Error("served application request is invalid");
      }
      const control = new DeviceControlClient(session as WorkloadRpcSession & DeviceControlSession, {
        onFrame: () => {},
      });
      try {
        await control.launchApp(payload.name, payload.args ?? "", signal);
        return { launched: true };
      } finally {
        await control.dispose();
      }
    }
    if (request.capability === "evidence.create") {
      const payload = payloadObject(request.payload);
      if (typeof payload.evidenceId !== "string" || typeof payload.caseId !== "string" ||
          typeof payload.sourcePath !== "string" || typeof payload.contentSha256 !== "string" ||
          typeof payload.contentLength !== "number" || typeof payload.mediaType !== "string") {
        throw new Error("served evidence request is invalid");
      }
      return new EvidenceClient(session).capture({
        evidenceId: payload.evidenceId,
        caseId: payload.caseId,
        sourcePath: payload.sourcePath,
        contentSha256: payload.contentSha256,
        contentLength: payload.contentLength,
        mediaType: payload.mediaType,
      }, signal);
    }
    throw new Error("served interface capability is unsupported");
  }, [session]);

  useEffect(() => () => monitorAbort.current?.abort(), []);
  useEffect(() => {
    let cancelled = false;
    void loadProjectRevisions(manifest.id).then((loaded) => {
      if (cancelled) return;
      setRevisions(loaded);
      const latest = loaded.at(-1);
      if (latest && !dirty.current) {
        setFiles({ ...latest.files });
        setSelected(latest.files[manifest.entrypoint] !== undefined ? manifest.entrypoint : Object.keys(latest.files)[0] ?? manifest.entrypoint);
        setRevisionMessage(`Restored revision ${latest.revision}`);
      }
    }, (reason) => {
      if (!cancelled) setRevisionMessage(reason instanceof Error ? reason.message : String(reason));
    });
    return () => { cancelled = true; };
  }, [manifest.entrypoint, manifest.id]);

  const saveRevision = async () => {
    try {
      const packaged = packageJavaScriptProject({ manifest, files });
      const digest = await sha256Hex(new TextEncoder().encode(packaged.canonical));
      const revision = (revisions.at(-1)?.revision ?? 0) + 1;
      const saved = await saveProjectRevision({
        id: `${manifest.id}-${revision}-${digest.slice(0, 16)}`,
        projectId: manifest.id,
        revision,
        digest,
        manifest,
        files,
      });
      if ("kind" in saved) throw new Error(`revision conflict: expected ${saved.expectedRevision}, received ${saved.actualRevision}`);
      const loaded = await loadProjectRevisions(manifest.id);
      setRevisions(loaded);
      dirty.current = false;
      setRevisionMessage(`Saved revision ${saved.revision} · ${saved.digest.slice(0, 12)}`);
    } catch (reason) {
      setRevisionMessage(reason instanceof Error ? reason.message : String(reason));
    }
  };

  const restoreRevision = (revision: number) => {
    try {
      const restored = restoreProjectRevision(manifest.id, revision);
      setFiles({ ...restored.files });
      setSelected(restored.files[manifest.entrypoint] !== undefined ? manifest.entrypoint : Object.keys(restored.files)[0] ?? manifest.entrypoint);
      dirty.current = false;
      setRevisionMessage(`Restored revision ${revision}`);
    } catch (reason) {
      setRevisionMessage(reason instanceof Error ? reason.message : String(reason));
    }
  };

  const applyStatus = (status: WorkloadStatus): WorkloadState => {
    const next = deviceWorkloadState(status.state);
    setState(next);
    return next;
  };

  const monitor = async (workloadId: string, controller: AbortController) => {
    let fromSequence = 1n;
    while (!controller.signal.aborted) {
      const snapshot = await client.inspect(workloadId, fromSequence, controller.signal);
      setFrames((current) => mergeConsoleFrames(current, snapshot.console));
      fromSequence = snapshot.status.nextSequence;
      const next = applyStatus(snapshot.status);
      if (TERMINAL_STATES.has(next)) return;
      await delay(250, controller.signal);
    }
  };

  const run = async () => {
    if (diagnostics.length > 0) return;
    monitorAbort.current?.abort();
    const controller = new AbortController();
    monitorAbort.current = controller;
    activeWorkload.current = null;
    activeDeployment.current = null;
    setFrames([]);
    setError(null);
    setTransfer(null);
    setState("starting");
    try {
      const packaged = packageJavaScriptProject({ manifest, files });
      await onRun?.(packaged.canonical);
      const deployment = await client.deployAndRunJavaScript(
        manifest,
        files,
        setTransfer,
        controller.signal,
      );
      activeWorkload.current = deployment.workloadId;
      activeDeployment.current = deployment;
      const next = applyStatus(deployment.status);
      if (!TERMINAL_STATES.has(next)) await monitor(deployment.workloadId, controller);
    } catch (reason) {
      if (isAbort(reason)) return;
      setState("failed");
      setError(reason instanceof Error ? reason.message : String(reason));
    }
  };

  const stop = async () => {
    const workloadId = activeWorkload.current;
    monitorAbort.current?.abort();
    if (!workloadId) return;
    setError(null);
    setState("cancelling");
    try {
      const status = await client.cancel(workloadId);
      applyStatus(status);
      await onStop?.();
    } catch (reason) {
      setState("failed");
      setError(reason instanceof Error ? reason.message : String(reason));
    }
  };

  const saveArtifact = async () => {
    const deployment = activeDeployment.current;
    if (!deployment || !artifactFile) return;
    setArtifactMessage("Uploading artifact");
    setError(null);
    try {
      const status = await client.uploadAndFinalizeArtifact(
        deployment,
        manifest.entrypoint,
        {
          artifactId: artifactFile.name,
          filename: artifactFile.name,
          data: new Uint8Array(await artifactFile.arrayBuffer()),
        },
        setTransfer,
      );
      applyStatus(status);
      setArtifactMessage(`Saved artifact ${artifactFile.name}`);
    } catch (reason) {
      setArtifactMessage("");
      setError(reason instanceof Error ? reason.message : String(reason));
    }
  };

  return <section aria-label="JavaScript workspace"><h2>{manifest.name}</h2><ProjectExplorer files={Object.keys(files)} selected={selected} onSelect={setSelected} /><CodeEditor path={selected} source={files[selected] ?? ""} onChange={(source) => { dirty.current = true; setFiles((current) => ({ ...current, [selected]: source })); }} /><DependencyManager lockSource={files[manifest.dependencies]} onImport={(imported) => { dirty.current = true; setFiles((current) => ({ ...current, ...imported })); }} /><section aria-label="Project revisions"><button type="button" onClick={() => void saveRevision()}>Save revision</button>{revisions.map((revision) => <button type="button" key={revision.id} onClick={() => restoreRevision(revision.revision)}>Restore revision {revision.revision}</button>)}{revisionMessage && <p role="status">{revisionMessage}</p>}</section><RunControls state={state} onRun={() => void run()} onStop={() => void stop()} /><section aria-label="Workload artifacts"><label>Artifact file<input aria-label="Artifact file" type="file" onChange={(event) => setArtifactFile(event.currentTarget.files?.[0] ?? null)} /></label><button type="button" disabled={!activeDeployment.current || !artifactFile} onClick={() => void saveArtifact()}>Save artifact</button>{artifactMessage && <p role="status">{artifactMessage}</p>}</section>{transfer && <p role="status">Uploading {transfer.operationId}: {transfer.sentBytes}/{transfer.totalBytes} bytes{transfer.acknowledged ? " verified" : ""}</p>}{error && <p role="alert">{error}</p>}<Diagnostics diagnostics={diagnostics} /><Console frames={frames} />{manifest.servedUi && <ServedAppHost loadBundle={loadServedBundle} capabilities={SERVED_BROKER_CAPABILITIES} authorize={authorizeServedOperation} dispatch={dispatchServedOperation} />}</section>;
}
