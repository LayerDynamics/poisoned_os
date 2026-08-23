import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { CommandStatus, MainSchema, type Main } from "../../generated/flipper_pb";
import {
  WorkloadConsoleFrameSchema,
  WorkloadConsoleType,
  WorkloadOperation,
  WorkloadRuntime,
  WorkloadState,
  WorkloadStatusSchema,
} from "../../generated/poison_workload_pb";
import { WorkloadClient, WorkloadClientError, javascriptCapabilityMask, type WorkloadRpcSession } from "./WorkloadClient";
import { validateJavaScriptManifest } from "./manifest";

const digest = "01".repeat(32);
const definition = {
  workloadId: "js-1",
  projectDigest: digest,
  capabilitiesDigest: digest,
  entrypoint: "/scripts/javascript/js-1/src/main.js",
  limits: {
    heapBytes: 32_768,
    sourceBytes: 4_096,
    modules: 4,
    parserDepth: 32,
    stackDepth: 32,
    fuel: 100_000,
    callbacks: 16,
    timers: 16,
    openHandles: 8,
    logs: 16_384,
    artifacts: 4,
    wallMs: 5_000,
    artifactBytes: 131_072,
  },
  capabilityMask: 0,
};

function status(request: Main, state = WorkloadState.QUEUED, accepted = true): Main {
  return create(MainSchema, {
    commandId: request.commandId,
    commandStatus: CommandStatus.OK,
    content: {
      case: "poisonWorkloadStatus",
      value: create(WorkloadStatusSchema, {
        workloadId: "js-1",
        runtime: WorkloadRuntime.JAVASCRIPT,
        state,
        nextSequence: 1n,
        accepted,
        message: accepted ? "accepted" : "rejected",
      }),
    },
  });
}

describe("WorkloadClient", () => {
  it("creates and runs a JavaScript workload through generated authenticated RPC messages", async () => {
    const operations: number[] = [];
    const session: WorkloadRpcSession = {
      async request(request) {
        expect(request.content.case).toBe("poisonWorkloadRequest");
        if (request.content.case === "poisonWorkloadRequest") operations.push(request.content.value.operation);
        return status(request, operations.length === 1 ? WorkloadState.QUEUED : WorkloadState.RUNNING);
      },
      async requestStream() { return []; },
    };
    const client = new WorkloadClient(session);
    await expect(client.createJavaScript(definition)).resolves.toMatchObject({ state: WorkloadState.QUEUED });
    await expect(client.run("js-1")).resolves.toMatchObject({ state: WorkloadState.RUNNING });
    expect(operations).toEqual([1, 2]);
  });

  it("maps declared module families to the exact managed-runtime capability mask", () => {
    expect(javascriptCapabilityMask(["storage.project.read", "gpio.read", "console.write"]))
      .toBe((1 << 7) | (1 << 6) | (1 << 1));
    expect(() => javascriptCapabilityMask(["native.ffi"]))
      .toThrow("unsupported JavaScript capability: native.ffi");
  });

  it("rejects Node asynchronous built-ins unless the runtime capability is declared", async () => {
    const session: WorkloadRpcSession = {
      async request() { throw new Error("device must not be called"); },
      async requestStream() { throw new Error("device must not be called"); },
    };
    const manifest = validateJavaScriptManifest({
      format: 1,
      id: "org.poisonedos.timers",
      name: "Timers",
      version: "1.0.0",
      language: "javascript",
      entrypoint: "src/main.js",
      runtime: "poison-mjs-1",
      runtimeApi: 1,
      firmwareApi: "1.0.0",
      capabilities: [],
      limits: { heapBytes: 32_768, wallTimeMs: 5_000, logBytes: 16_384, artifactBytes: 131_072 },
      dependencies: "poison-js.lock",
      servedUi: null,
    });
    await expect(new WorkloadClient(session).deployAndRunJavaScript(manifest, {
      "src/main.js": "require('timers').setTimeout(function() { print('done'); }, 1);",
      "poison-js.lock": '{"schema":"poison.javascript.lock/v1","runtime":"poison-mjs-1","entrypoint":"src/main.js","dependencies":[]}\n',
    })).rejects.toThrow("runtime capability");
  });

  it("rejects source the on-device mJS parser cannot execute before transport", async () => {
    const session: WorkloadRpcSession = {
      async request() { throw new Error("device must not be called"); },
      async requestStream() { throw new Error("device must not be called"); },
    };
    const manifest = validateJavaScriptManifest({
      format: 1, id: "org.poisonedos.syntax", name: "Syntax", version: "1.0.0",
      language: "javascript", entrypoint: "src/main.js", runtime: "poison-mjs-1",
      runtimeApi: 1, firmwareApi: "1.0.0", capabilities: [],
      limits: { heapBytes: 32_768, wallTimeMs: 5_000, logBytes: 16_384, artifactBytes: 131_072 },
      dependencies: "poison-js.lock", servedUi: null,
    });
    await expect(new WorkloadClient(session).deployAndRunJavaScript(manifest, {
      "src/main.js": "throw new Error('not executable');",
      "poison-js.lock": '{"schema":"poison.javascript.lock/v1","runtime":"poison-mjs-1","entrypoint":"src/main.js","dependencies":[]}\n',
    })).rejects.toThrow("MJS_KEYWORD_UNSUPPORTED");
  });

  it("reconciles ordered console frames with the terminal status receipt", async () => {
    const session: WorkloadRpcSession = {
      async request(request) { return status(request); },
      async requestStream(request, maximum) {
        expect(maximum).toBe(34);
        return [
          create(MainSchema, {
            commandId: request.commandId,
            commandStatus: CommandStatus.OK,
            hasNext: true,
            content: {
              case: "poisonWorkloadConsole",
              value: create(WorkloadConsoleFrameSchema, {
                workloadId: "js-1",
                sequence: 1n,
                type: WorkloadConsoleType.WORKLOAD_CONSOLE_STDOUT,
                text: "ready",
              }),
            },
          }),
          create(MainSchema, {
            commandId: request.commandId,
            commandStatus: CommandStatus.OK,
            content: {
              case: "poisonWorkloadStatus",
              value: create(WorkloadStatusSchema, {
                workloadId: "js-1",
                runtime: WorkloadRuntime.JAVASCRIPT,
                state: WorkloadState.COMPLETED,
                nextSequence: 2n,
                accepted: true,
                message: "accepted",
              }),
            },
          }),
        ];
      },
    };
    await expect(new WorkloadClient(session).inspect("js-1")).resolves.toMatchObject({
      status: { state: WorkloadState.COMPLETED },
      console: [{ sequence: 1, source: "stdout", text: "ready" }],
    });
  });

  it("finalizes only project-scoped artifacts and binds optional evidence identifiers", async () => {
    const captured: Main[] = [];
    const session: WorkloadRpcSession = {
      async request(request) {
        captured.push(request);
        return status(request, WorkloadState.RUNNING);
      },
      async requestStream() { return []; },
    };
    const client = new WorkloadClient(session);
    await client.finalizeArtifact("js-1", {
      artifactId: "report.json",
      path: "/scripts/javascript/js-1/versions/abc/artifacts/report.json",
      size: 10,
      sha256: digest,
      evidence: { evidenceId: "ev-1", caseId: "case-1" },
    });
    expect(captured[0]?.content.case).toBe("poisonWorkloadRequest");
    if (captured[0]?.content.case === "poisonWorkloadRequest") {
      expect(captured[0].content.value).toMatchObject({
        operation: WorkloadOperation.FINALIZE_ARTIFACT,
        evidenceRequested: true,
        evidenceId: "ev-1",
        caseId: "case-1",
      });
    }
    await expect(client.finalizeArtifact("js-1", {
      artifactId: "bad",
      path: "/evidence/private.bin",
      size: 1,
      sha256: digest,
    })).rejects.toBeInstanceOf(WorkloadClientError);
  });

  it("uploads an artifact beneath the executed immutable revision before finalizing it", async () => {
    const requests: Main[] = [];
    const session: WorkloadRpcSession = {
      async request(request) {
        requests.push(request);
        if (request.content.case !== "poisonWorkloadRequest") {
          return create(MainSchema, {
            commandId: request.commandId,
            commandStatus: CommandStatus.OK,
            content: { case: "empty", value: {} },
          });
        }
        return status(request, WorkloadState.COMPLETED);
      },
      async requestStream() { return []; },
    };
    const client = new WorkloadClient(session);
    await client.uploadAndFinalizeArtifact({
      workloadId: "js-1",
      projectDigest: digest,
      capabilitiesDigest: digest,
      entrypoint: `/scripts/javascript/org.poisonedos.rpc/versions/${digest}/src/main.js`,
      status: create(WorkloadStatusSchema, {
        workloadId: "js-1",
        runtime: WorkloadRuntime.JAVASCRIPT,
        state: WorkloadState.COMPLETED,
      }),
    }, "src/main.js", {
      artifactId: "report.json",
      filename: "report.json",
      data: new TextEncoder().encode('{"result":7}\n'),
    });
    const begin = requests.find((request) => request.content.case === "poisonFileTransferBegin");
    expect(begin?.content.case === "poisonFileTransferBegin" && begin.content.value.path)
      .toBe(`/scripts/javascript/org.poisonedos.rpc/versions/${digest}/artifacts/report.json`);
    const finalized = requests.at(-1);
    expect(finalized?.content.case).toBe("poisonWorkloadRequest");
    if (finalized?.content.case === "poisonWorkloadRequest") {
      expect(finalized.content.value).toMatchObject({
        operation: WorkloadOperation.FINALIZE_ARTIFACT,
        workloadId: "js-1",
        artifactId: "report.json",
        artifactPath: `/scripts/javascript/org.poisonedos.rpc/versions/${digest}/artifacts/report.json`,
      });
    }
  });

  it("uploads an immutable project revision before creating and running it", async () => {
    const requests: Main[] = [];
    const session: WorkloadRpcSession = {
      async request(request) {
        requests.push(request);
        if (request.content.case !== "poisonWorkloadRequest") {
          return create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, content: { case: "empty", value: {} } });
        }
        return create(MainSchema, {
          commandId: request.commandId,
          commandStatus: CommandStatus.OK,
          content: {
            case: "poisonWorkloadStatus",
            value: create(WorkloadStatusSchema, {
              workloadId: request.content.value.workloadId,
              runtime: WorkloadRuntime.JAVASCRIPT,
              state: request.content.value.operation === 1 ? WorkloadState.QUEUED : WorkloadState.RUNNING,
              nextSequence: 1n,
              accepted: true,
              message: "accepted",
            }),
          },
        });
      },
      async requestStream() { return []; },
    };
    const manifest = validateJavaScriptManifest({
      format: 1,
      id: "org.poisonedos.rpc",
      name: "RPC",
      version: "1.0.0",
      language: "javascript",
      entrypoint: "src/main.js",
      runtime: "poison-mjs-1",
      runtimeApi: 1,
      firmwareApi: "1.0.0",
      capabilities: ["storage.project.read"],
      limits: { heapBytes: 32_768, wallTimeMs: 5_000, logBytes: 16_384, artifactBytes: 131_072 },
      dependencies: "poison-js.lock",
      servedUi: null,
    });
    const deployment = await new WorkloadClient(session).deployAndRunJavaScript(
      manifest,
      {
        "src/main.js": "var path = require('path'); print(path.join('poison', 'ready'))",
        "src/empty.js": "",
        "poison-js.lock": '{"schema":"poison.javascript.lock/v1","runtime":"poison-mjs-1","entrypoint":"src/main.js","dependencies":[]}\n',
      },
    );
    expect(deployment.status.state).toBe(WorkloadState.RUNNING);
    expect(deployment.entrypoint).toContain(`/versions/${deployment.projectDigest}/src/main.js`);
    const beginPaths = requests.flatMap((request) => request.content.case === "poisonFileTransferBegin" ? [request.content.value.path] : []);
    expect(beginPaths).toEqual([
      expect.stringMatching(/\/versions\/[0-9a-f]{64}\/project\.json$/),
      expect.stringMatching(/\/versions\/[0-9a-f]{64}\/_poison\/node\/path\.js$/),
      expect.stringMatching(/\/versions\/[0-9a-f]{64}\/poison-js\.lock$/),
      expect.stringMatching(/\/versions\/[0-9a-f]{64}\/src\/empty\.js$/),
      expect.stringMatching(/\/versions\/[0-9a-f]{64}\/src\/main\.js$/),
    ]);
    expect(requests.slice(-2).map((request) => request.content.case === "poisonWorkloadRequest" && request.content.value.operation)).toEqual([1, 2]);
    const projectUpload = requests.find((request) => request.content.case === "poisonFileTransferBegin" && request.content.value.path.endsWith("/project.json"));
    expect(projectUpload?.content.case === "poisonFileTransferBegin" && projectUpload.content.value.size).toBeGreaterThan(0n);
  });

  it("rejects a lock whose entrypoint does not describe the executed project", async () => {
    const session: WorkloadRpcSession = {
      async request() { throw new Error("device must not be called"); },
      async requestStream() { throw new Error("device must not be called"); },
    };
    const manifest = validateJavaScriptManifest({
      format: 1, id: "org.poisonedos.entry", name: "Entry", version: "1.0.0",
      language: "javascript", entrypoint: "src/main.js", runtime: "poison-mjs-1",
      runtimeApi: 1, firmwareApi: "1.0.0", capabilities: [],
      limits: { heapBytes: 32_768, wallTimeMs: 5_000, logBytes: 16_384, artifactBytes: 131_072 },
      dependencies: "poison-js.lock", servedUi: null,
    });
    await expect(new WorkloadClient(session).deployAndRunJavaScript(manifest, {
      "src/main.js": "print('ready')",
      "poison-js.lock": '{"schema":"poison.javascript.lock/v1","runtime":"poison-mjs-1","entrypoint":"other.js","dependencies":[]}\n',
    })).rejects.toThrow("entrypoint");
  });

  it("rejects a deployment before transport when its dependency lock is missing", async () => {
    const session: WorkloadRpcSession = {
      async request() { throw new Error("device must not be called"); },
      async requestStream() { throw new Error("device must not be called"); },
    };
    const manifest = validateJavaScriptManifest({
      format: 1,
      id: "org.poisonedos.locked",
      name: "Locked",
      version: "1.0.0",
      language: "javascript",
      entrypoint: "main.js",
      runtime: "poison-mjs-1",
      runtimeApi: 1,
      firmwareApi: "1.0.0",
      capabilities: [],
      limits: { heapBytes: 32_768, wallTimeMs: 5_000, logBytes: 16_384, artifactBytes: 131_072 },
      dependencies: "poison-js.lock",
      servedUi: null,
    });
    await expect(new WorkloadClient(session).deployAndRunJavaScript(manifest, { "main.js": "print('ready')" }))
      .rejects.toThrow("dependency lock is missing");
  });

  it("fails closed on mutated definitions, rejection, and console gaps", async () => {
    const rejecting: WorkloadRpcSession = {
      async request(request) { return status(request, WorkloadState.QUEUED, false); },
      async requestStream() { return []; },
    };
    const client = new WorkloadClient(rejecting);
    await expect(client.createJavaScript({ ...definition, projectDigest: `A${digest.slice(1)}` }))
      .rejects.toEqual(new WorkloadClientError("invalid", "JavaScript workload definition is invalid"));
    await expect(client.createJavaScript(definition))
      .rejects.toEqual(new WorkloadClientError("rejected", "rejected"));

    const gapped: WorkloadRpcSession = {
      async request(request) { return status(request); },
      async requestStream(request) {
        return [
          create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, hasNext: true, content: { case: "poisonWorkloadConsole", value: create(WorkloadConsoleFrameSchema, { workloadId: "js-1", sequence: 1n, type: WorkloadConsoleType.WORKLOAD_CONSOLE_LOG, text: "one" }) } }),
          create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, hasNext: true, content: { case: "poisonWorkloadConsole", value: create(WorkloadConsoleFrameSchema, { workloadId: "js-1", sequence: 3n, type: WorkloadConsoleType.WORKLOAD_CONSOLE_LOG, text: "three" }) } }),
          create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, content: { case: "poisonWorkloadStatus", value: create(WorkloadStatusSchema, { workloadId: "js-1", runtime: WorkloadRuntime.JAVASCRIPT, state: WorkloadState.RUNNING, nextSequence: 4n, accepted: true }) } }),
        ];
      },
    };
    await expect(new WorkloadClient(gapped).inspect("js-1"))
      .rejects.toEqual(new WorkloadClientError("gap", "workload console sequence is invalid"));
  });
});
