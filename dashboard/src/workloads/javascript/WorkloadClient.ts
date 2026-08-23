import { create } from "@bufbuild/protobuf";
import { CommandStatus, MainSchema, type Main } from "../../generated/flipper_pb";
import {
  WorkloadConsoleType,
  WorkloadLimitsSchema,
  WorkloadOperation,
  WorkloadRequestSchema,
  WorkloadRuntime,
  WorkloadState,
  type WorkloadStatus,
} from "../../generated/poison_workload_pb";
import type { ConsoleFrame } from "./Console";
import { FileTransferQueue, RpcFileTransferTransport, sha256Hex, type TransferProgress } from "../../files/FileTransferQueue";
import { packageJavaScriptProject } from "./PackageProject";
import { verifyProjectDependencies } from "./DependencyManager";
import { runtimeBuiltinCapabilities, runtimeBuiltinFiles } from "./RuntimeBuiltins";
import { validateJavaScriptSyntax } from "./SyntaxValidator";
import type { JavaScriptManifest } from "./manifest";

const DIGEST = /^[0-9a-f]{64}$/;
const WORKLOAD_ID = /^[a-z0-9][a-z0-9_-]{0,62}$/;
const JAVASCRIPT_ENTRYPOINT = /^\/scripts\/(?:[^/\\\0.][^/\\\0]*\/)*[^/\\\0.][^/\\\0]*\.m?js$/;

export interface WorkloadRpcSession {
  request(request: Main, signal?: AbortSignal): Promise<Main>;
  requestStream(request: Main, maxResponses?: number, signal?: AbortSignal): Promise<readonly Main[]>;
}

export interface JavaScriptWorkloadLimits {
  readonly heapBytes: number;
  readonly sourceBytes: number;
  readonly modules: number;
  readonly parserDepth: number;
  readonly stackDepth: number;
  readonly fuel: number;
  readonly callbacks: number;
  readonly timers: number;
  readonly openHandles: number;
  readonly logs: number;
  readonly artifacts: number;
  readonly wallMs: number;
  readonly artifactBytes: number;
}

export interface JavaScriptWorkloadDefinition {
  readonly workloadId: string;
  readonly projectDigest: string;
  readonly capabilitiesDigest: string;
  readonly entrypoint: string;
  readonly limits: JavaScriptWorkloadLimits;
  readonly capabilityMask: number;
}

export interface WorkloadSnapshot {
  readonly status: WorkloadStatus;
  readonly console: readonly ConsoleFrame[];
}

export interface JavaScriptDeployment {
  readonly workloadId: string;
  readonly projectDigest: string;
  readonly capabilitiesDigest: string;
  readonly entrypoint: string;
  readonly status: WorkloadStatus;
}

export class WorkloadClientError extends Error {
  public constructor(
    public readonly code: "invalid" | "protocol" | "rejected" | "gap",
    message: string,
  ) {
    super(message);
    this.name = "WorkloadClientError";
  }
}

function boundedInteger(value: number, minimum: number, maximum: number): boolean {
  return Number.isSafeInteger(value) && value >= minimum && value <= maximum;
}

function validateDefinition(definition: JavaScriptWorkloadDefinition): void {
  const limits = definition.limits;
  if (!WORKLOAD_ID.test(definition.workloadId) || !DIGEST.test(definition.projectDigest) ||
      !DIGEST.test(definition.capabilitiesDigest) || definition.entrypoint.length > 256 ||
      !JAVASCRIPT_ENTRYPOINT.test(definition.entrypoint) ||
      !boundedInteger(limits.heapBytes, 1, 64 * 1024) ||
      !boundedInteger(limits.sourceBytes, 1, 256 * 1024) ||
      !boundedInteger(limits.modules, 1, 32) || !boundedInteger(limits.parserDepth, 1, 64) ||
      !boundedInteger(limits.stackDepth, 1, 64) || !boundedInteger(limits.fuel, 1, 10_000_000) ||
      !boundedInteger(limits.callbacks, 0, 64) || !boundedInteger(limits.timers, 0, 64) ||
      !boundedInteger(limits.openHandles, 0, 32) || !boundedInteger(limits.logs, 1, 64 * 1024) ||
      !boundedInteger(limits.artifacts, 1, 8) || !boundedInteger(limits.artifactBytes, 1, 8 * 1024 * 1024) ||
      !boundedInteger(limits.wallMs, 1, 60_000) ||
      !boundedInteger(definition.capabilityMask, 0, 0x7ff)) {
    throw new WorkloadClientError("invalid", "JavaScript workload definition is invalid");
  }
}

const capabilityBits: Readonly<Record<string, number>> = {
  device: 1 << 0,
  runtime: 1 << 1,
  console: 1 << 1,
  ui: 1 << 2,
  notification: 1 << 3,
  badusb: 1 << 4,
  serial: 1 << 5,
  gpio: 1 << 6,
  storage: 1 << 7,
  crypto: 1 << 8,
  compute: 1 << 9,
  evidence: 1 << 10,
};

export function javascriptCapabilityMask(capabilities: readonly string[]): number {
  let mask = 0;
  for (const capability of capabilities) {
    const family = capability.split(".", 1)[0];
    const bit = capabilityBits[family];
    if (bit === undefined) {
      throw new WorkloadClientError("invalid", `unsupported JavaScript capability: ${capability}`);
    }
    mask |= bit;
  }
  return mask;
}

function validateWorkloadId(workloadId: string): void {
  if (!WORKLOAD_ID.test(workloadId)) {
    throw new WorkloadClientError("invalid", "workload id is invalid");
  }
}

const consoleSources: Readonly<Record<number, ConsoleFrame["source"]>> = {
  [WorkloadConsoleType.WORKLOAD_CONSOLE_STDOUT]: "stdout",
  [WorkloadConsoleType.WORKLOAD_CONSOLE_STDERR]: "stderr",
  [WorkloadConsoleType.WORKLOAD_CONSOLE_LOG]: "log",
  [WorkloadConsoleType.WORKLOAD_CONSOLE_EVENT]: "event",
  [WorkloadConsoleType.WORKLOAD_CONSOLE_TRUNCATION]: "truncation",
};

export class WorkloadClient {
  private nextCommandId = 25_000;

  public constructor(private readonly session: WorkloadRpcSession) {}

  public async deployAndRunJavaScript(
    manifest: JavaScriptManifest,
    files: Readonly<Record<string, string>>,
    onTransfer?: (progress: TransferProgress) => void,
    signal?: AbortSignal,
  ): Promise<JavaScriptDeployment> {
    let runtimeFiles: Readonly<Record<string, string>>;
    try {
      const declaredCapabilityFamilies = new Set(
        manifest.capabilities.map((capability) => capability.split(".", 1)[0]),
      );
      const missingBuiltinCapabilities = [...runtimeBuiltinCapabilities(files)]
        .filter((capability) => !declaredCapabilityFamilies.has(capability));
      if(missingBuiltinCapabilities.length > 0) {
        throw new Error(
          `Node built-ins require declared ${missingBuiltinCapabilities.sort().join(", ")} capability`,
        );
      }
      runtimeFiles = runtimeBuiltinFiles(files);
      const diagnostics = validateJavaScriptSyntax(
        { ...files, ...runtimeFiles },
        manifest.entrypoint,
      );
      if(diagnostics.length > 0) {
        const diagnostic = diagnostics[0];
        throw new Error(
          `${diagnostic.code} at ${diagnostic.file}:${diagnostic.line}:${diagnostic.column}: ${diagnostic.message}`,
        );
      }
    } catch(error) {
      throw new WorkloadClientError("invalid", error instanceof Error ? error.message : String(error));
    }
    try {
      const lockSource = files[manifest.dependencies];
      await verifyProjectDependencies(lockSource, files);
      const lock = JSON.parse(lockSource ?? "null") as { entrypoint?: unknown } | null;
      if(lock?.entrypoint !== manifest.entrypoint) {
        throw new Error("dependency lock entrypoint does not match the JavaScript manifest");
      }
    } catch (error) {
      throw new WorkloadClientError("invalid", error instanceof Error ? error.message : String(error));
    }
    let packaged;
    try {
      packaged = packageJavaScriptProject({
        manifest,
        files: { ...files, ...runtimeFiles },
      });
    } catch(error) {
      throw new WorkloadClientError("invalid", error instanceof Error ? error.message : String(error));
    }
    const encoder = new TextEncoder();
    const fileBytes = packaged.files.map(({ path, source }) => ({ path, data: encoder.encode(source) }));
    const projectFiles = await Promise.all(fileBytes.map(async ({ path, data }) => ({
      path,
      sha256: await sha256Hex(data),
      bytes: data.byteLength,
    })));
    const projectRecord = `${JSON.stringify({
      format: 1,
      runtime: manifest.runtime,
      entrypoint: manifest.entrypoint,
      dependencies: manifest.dependencies,
      files: projectFiles,
    })}\n`;
    const projectBytes = encoder.encode(projectRecord);
    const projectDigest = await sha256Hex(projectBytes);
    const capabilitiesDigest = await sha256Hex(encoder.encode(JSON.stringify([...manifest.capabilities].sort())));
    const workloadId = `js-${projectDigest.slice(0, 24)}`;
    const versionRoot = `/scripts/javascript/${manifest.id}/versions/${projectDigest}`;
    const transfer = new FileTransferQueue(new RpcFileTransferTransport(this.session));
    const members = [
      { path: "project.json", data: projectBytes },
      ...fileBytes,
    ];
    for (const [index, member] of members.entries()) {
      const path = `${versionRoot}/${member.path}`;
      await transfer.upload(
        `${workloadId}-${index}`,
        path,
        member.data,
        await sha256Hex(member.data),
        onTransfer,
        signal,
      );
    }
    const executableFiles = packaged.files.filter((file) => /\.(?:js|mjs|cjs)$/.test(file.path));
    const sourceBytes = executableFiles.reduce(
      (total, file) => total + encoder.encode(file.source).byteLength,
      0,
    );
    const entrypoint = `${versionRoot}/${manifest.entrypoint}`;
    await this.createJavaScript({
      workloadId,
      projectDigest,
      capabilitiesDigest,
      entrypoint,
      limits: {
        heapBytes: Math.min(manifest.limits.heapBytes, 64 * 1024),
        sourceBytes,
        modules: executableFiles.length,
        parserDepth: 32,
        stackDepth: 32,
        fuel: Math.min(10_000_000, Math.max(100_000, sourceBytes * 256)),
        callbacks: 64,
        timers: 64,
        openHandles: 32,
        logs: Math.min(manifest.limits.logBytes, 64 * 1024),
        artifacts: Math.min(8, Math.max(1, Math.ceil(manifest.limits.artifactBytes / (128 * 1024)))),
        wallMs: Math.min(manifest.limits.wallTimeMs, 60_000),
        artifactBytes: Math.min(manifest.limits.artifactBytes, 8 * 1024 * 1024),
      },
      capabilityMask: javascriptCapabilityMask(manifest.capabilities),
    }, signal);
    const status = await this.run(workloadId, false, signal);
    return { workloadId, projectDigest, capabilitiesDigest, entrypoint, status };
  }

  public async uploadAndFinalizeArtifact(
    deployment: JavaScriptDeployment,
    projectEntrypoint: string,
    artifact: {
      readonly artifactId: string;
      readonly filename: string;
      readonly data: Uint8Array;
      readonly evidence?: { readonly evidenceId: string; readonly caseId: string };
    },
    onTransfer?: (progress: TransferProgress) => void,
    signal?: AbortSignal,
  ): Promise<WorkloadStatus> {
    validateWorkloadId(deployment.workloadId);
    if (!DIGEST.test(deployment.projectDigest) ||
        !/^(?:[A-Za-z0-9._-]+\/)*[A-Za-z0-9._-]+\.m?js$/.test(projectEntrypoint) ||
        !deployment.entrypoint.endsWith(`/${projectEntrypoint}`) ||
        !deployment.entrypoint.includes(`/versions/${deployment.projectDigest}/`) ||
        !/^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$/.test(artifact.artifactId) ||
        !/^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$/.test(artifact.filename) ||
        artifact.data.byteLength < 1 || artifact.data.byteLength > 8 * 1024 * 1024) {
      throw new WorkloadClientError("invalid", "workload artifact upload is invalid");
    }
    const versionRoot = deployment.entrypoint.slice(0, -projectEntrypoint.length);
    const path = `${versionRoot}artifacts/${artifact.filename}`;
    const sha256 = await sha256Hex(artifact.data);
    const transfer = new FileTransferQueue(new RpcFileTransferTransport(this.session));
    await transfer.upload(
      `artifact-${deployment.workloadId.slice(0, 35)}-${sha256.slice(0, 16)}`,
      path,
      artifact.data,
      sha256,
      onTransfer,
      signal,
    );
    return this.finalizeArtifact(deployment.workloadId, {
      artifactId: artifact.artifactId,
      path,
      size: artifact.data.byteLength,
      sha256,
      ...(artifact.evidence ? { evidence: artifact.evidence } : {}),
    }, signal);
  }

  public async createJavaScript(
    definition: JavaScriptWorkloadDefinition,
    signal?: AbortSignal,
  ): Promise<WorkloadStatus> {
    validateDefinition(definition);
    return this.mutate(create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonWorkloadRequest",
        value: create(WorkloadRequestSchema, {
          operation: WorkloadOperation.CREATE,
          workloadId: definition.workloadId,
          runtime: WorkloadRuntime.JAVASCRIPT,
          projectDigest: definition.projectDigest,
          capabilitiesDigest: definition.capabilitiesDigest,
          entrypoint: definition.entrypoint,
          limits: create(WorkloadLimitsSchema, definition.limits),
          capabilityMask: definition.capabilityMask,
        }),
      },
    }), definition.workloadId, signal);
  }

  public async run(workloadId: string, evidenceRequested = false, signal?: AbortSignal): Promise<WorkloadStatus> {
    validateWorkloadId(workloadId);
    return this.mutate(this.operation(workloadId, WorkloadOperation.RUN, evidenceRequested), workloadId, signal);
  }

  public async cancel(workloadId: string, signal?: AbortSignal): Promise<WorkloadStatus> {
    validateWorkloadId(workloadId);
    return this.mutate(this.operation(workloadId, WorkloadOperation.CANCEL), workloadId, signal);
  }

  public async finalizeArtifact(
    workloadId: string,
    artifact: {
      readonly artifactId: string;
      readonly path: string;
      readonly size: number;
      readonly sha256: string;
      readonly evidence?: { readonly evidenceId: string; readonly caseId: string };
    },
    signal?: AbortSignal,
  ): Promise<WorkloadStatus> {
    validateWorkloadId(workloadId);
    if (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$/.test(artifact.artifactId) ||
        !/^\/scripts\/(?:[^/\\\0.][^/\\\0]*\/)*artifacts\/[^/\\\0.][^/\\\0]*$/.test(artifact.path) ||
        artifact.path.length > 256 || !boundedInteger(artifact.size, 1, 8 * 1024 * 1024) ||
        !DIGEST.test(artifact.sha256) ||
        (artifact.evidence !== undefined &&
          (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(artifact.evidence.evidenceId) ||
           !/^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(artifact.evidence.caseId)))) {
      throw new WorkloadClientError("invalid", "workload artifact request is invalid");
    }
    return this.mutate(create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonWorkloadRequest",
        value: create(WorkloadRequestSchema, {
          operation: WorkloadOperation.FINALIZE_ARTIFACT,
          workloadId,
          artifactId: artifact.artifactId,
          artifactPath: artifact.path,
          artifactSize: BigInt(artifact.size),
          artifactSha256: artifact.sha256,
          evidenceRequested: artifact.evidence !== undefined,
          evidenceId: artifact.evidence?.evidenceId ?? "",
          caseId: artifact.evidence?.caseId ?? "",
        }),
      },
    }), workloadId, signal);
  }

  public async inspect(workloadId: string, fromSequence = 0n, signal?: AbortSignal): Promise<WorkloadSnapshot> {
    validateWorkloadId(workloadId);
    if (fromSequence < 0n) throw new WorkloadClientError("invalid", "console sequence is invalid");
    const request = create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonWorkloadRequest",
        value: create(WorkloadRequestSchema, {
          operation: WorkloadOperation.INSPECT,
          workloadId,
          fromSequence,
        }),
      },
    });
    const responses = await this.session.requestStream(request, 34, signal);
    if (responses.length === 0) throw new WorkloadClientError("protocol", "device omitted workload status");
    const console: ConsoleFrame[] = [];
    let priorSequence: bigint | null = null;
    let status: WorkloadStatus | null = null;
    for (const [index, response] of responses.entries()) {
      if (response.commandStatus !== CommandStatus.OK || response.commandId !== request.commandId) {
        throw new WorkloadClientError("protocol", "device returned a mismatched workload response");
      }
      if (response.content.case === "poisonWorkloadConsole" && response.hasNext && !status) {
        const frame = response.content.value;
        const source = consoleSources[frame.type];
        if (frame.workloadId !== workloadId || !source || frame.sequence < 1n ||
            frame.sequence < fromSequence || (priorSequence !== null && frame.sequence !== priorSequence + 1n)) {
          throw new WorkloadClientError("gap", "workload console sequence is invalid");
        }
        console.push({ sequence: Number(frame.sequence), source, text: frame.text });
        priorSequence = frame.sequence;
      } else if (response.content.case === "poisonWorkloadStatus" && !response.hasNext &&
                 index === responses.length - 1) {
        status = response.content.value;
      } else {
        throw new WorkloadClientError("protocol", "device returned a malformed workload stream");
      }
    }
    if (!status) throw new WorkloadClientError("protocol", "device omitted workload status");
    this.validateStatus(status, workloadId);
    if (priorSequence !== null && status.nextSequence !== priorSequence + 1n) {
      throw new WorkloadClientError("gap", "workload status does not reconcile console sequence");
    }
    return { status, console };
  }

  private operation(workloadId: string, operation: WorkloadOperation, evidenceRequested = false): Main {
    return create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonWorkloadRequest",
        value: create(WorkloadRequestSchema, { operation, workloadId, evidenceRequested }),
      },
    });
  }

  private async mutate(request: Main, workloadId: string, signal?: AbortSignal): Promise<WorkloadStatus> {
    const response = await this.session.request(request, signal);
    if (response.commandId !== request.commandId || response.commandStatus !== CommandStatus.OK ||
        response.hasNext || response.content.case !== "poisonWorkloadStatus") {
      throw new WorkloadClientError("protocol", "device returned an invalid workload status");
    }
    this.validateStatus(response.content.value, workloadId);
    return response.content.value;
  }

  private validateStatus(status: WorkloadStatus, workloadId: string): void {
    if (status.workloadId !== workloadId || status.runtime === WorkloadRuntime.UNSPECIFIED ||
        status.state === WorkloadState.UNSPECIFIED || status.nextSequence < 1n ||
        !boundedInteger(status.effectiveCapabilityMask, 0, 0x7ff)) {
      throw new WorkloadClientError("protocol", "device returned malformed workload state");
    }
    if (!status.accepted) throw new WorkloadClientError("rejected", status.message || "device rejected workload operation");
  }
}
