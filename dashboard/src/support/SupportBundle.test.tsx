import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { MainSchema, type Main } from "../generated/flipper_pb";
import { DiagnosticCountersSchema, DiagnosticEventSchema } from "../generated/poison_diagnostics_pb";
import { createSupportBundle, requestDiagnosticSnapshot } from "./SupportBundle";

describe("support bundle", () => {
  it("requires preview consent and emits digest-only references", () => {
    const counters = { sessionEstablished: 0, transportErrors: 0, droppedFrames: 0, retriedFrames: 0, commandFailures: 0, appCrashes: 0, policyDenials: 0, packageVerifications: 0, packageRevocations: 0, updateStages: 0, updateHealth: 0, updateRollbacks: 0, recoveries: 0, javascriptStarts: 0, javascriptTerminals: 0, javascriptCrashes: 0, javascriptLimits: 0, javascriptRecoveries: 0 };
    expect(() => createSupportBundle({ acceptedAtMs: 0, components: [], counters, events: [], files: [] })).toThrow();
    const bundle = createSupportBundle({ acceptedAtMs: 10, components: [], counters: { ...counters, sessionEstablished: 1 }, events: [], files: [{ path: "/ext/log.txt", sha256: "00".repeat(32), size: 1 }] });
    expect(bundle.schema).toBe("poison.support-bundle/v1");
    expect(bundle.consent.previewed).toBe(true);
  });

  it("requests bounded live counters and ordered events", async () => {
    const client = {
      async requestStream(request: Main, maxResponses = 0): Promise<readonly Main[]> {
        expect(request.content.case).toBe("poisonDiagnosticSnapshotRequest");
        expect(maxResponses).toBe(17);
        return [
          create(MainSchema, { commandId: request.commandId, hasNext: true, content: { case: "poisonDiagnosticCounters", value: create(DiagnosticCountersSchema, { packageVerifications: 3, updateStages: 2 }) } }),
          create(MainSchema, { commandId: request.commandId, content: { case: "poisonDiagnosticEvent", value: create(DiagnosticEventSchema, { eventId: 4n, category: "update-stage", summary: "update staged", timestampMs: 9n, correlationDigest: new Uint8Array(32).fill(1) }) } }),
        ];
      },
    };
    const snapshot = await requestDiagnosticSnapshot(client, 50);
    expect(snapshot.counters.packageVerifications).toBe(3);
    expect(snapshot.events).toEqual([{ eventId: 4, category: "update-stage", summary: "update staged", timestampMs: 9, correlationDigest: "01".repeat(32) }]);
  });
});
