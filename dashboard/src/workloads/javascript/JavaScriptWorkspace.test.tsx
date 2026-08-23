import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";
import { WorkloadState } from "../../generated/poison_workload_pb";
import { JavaScriptWorkspace, deviceWorkloadState } from "./JavaScriptWorkspace";
import type { WorkloadRpcSession } from "./WorkloadClient";
import { validateJavaScriptManifest } from "./manifest";

const manifest = validateJavaScriptManifest({
  format: 1,
  id: "org.poisonedos.workspace",
  name: "Authenticated JavaScript",
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

describe("JavaScriptWorkspace", () => {
  it("renders the authenticated device workspace without fabricated console controls", () => {
    const session: WorkloadRpcSession = {
      async request() { throw new Error("not invoked while rendering"); },
      async requestStream() { throw new Error("not invoked while rendering"); },
    };
    const markup = renderToStaticMarkup(<JavaScriptWorkspace session={session} manifest={manifest} initialFiles={{ "src/main.js": "print('ready')" }} />);
    expect(markup).toContain("Authenticated JavaScript");
    expect(markup).toContain("JavaScript run controls");
    expect(markup).toContain("Offline dependencies");
    expect(markup).toContain("Save revision");
    expect(markup).toContain("Save artifact");
    expect(markup).not.toContain("Record event");
  });

  it("preserves every device terminal state instead of collapsing receipts", () => {
    expect(deviceWorkloadState(WorkloadState.COMPLETED)).toBe("completed");
    expect(deviceWorkloadState(WorkloadState.TIMED_OUT)).toBe("timed-out");
    expect(deviceWorkloadState(WorkloadState.CRASHED)).toBe("crashed");
    expect(deviceWorkloadState(WorkloadState.DISCONNECTED)).toBe("disconnected");
  });
});
