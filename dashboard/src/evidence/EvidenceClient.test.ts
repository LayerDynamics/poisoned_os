import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import {
  AnnotationSchema,
  CaseSchema,
  EvidenceRecordSchema,
  ExportManifestSchema,
} from "../generated/poison_evidence_pb";
import { EvidenceClient } from "./EvidenceClient";

describe("EvidenceClient", () => {
  it("captures an uploaded device file through the registered evidence RPC", async () => {
    const digest = "a".repeat(64);
    const client = new EvidenceClient({
      async request(request) {
        if (request.content.case !== "poisonEvidenceRecord") throw new Error("wrong request");
        return create(MainSchema, {
          commandId: request.commandId,
          commandStatus: CommandStatus.OK,
          content: {
            case: "poisonEvidenceRecord",
            value: create(EvidenceRecordSchema, {
              ...request.content.value,
              auditSha256: "b".repeat(64),
              previousAuditSha256: "0".repeat(64),
            }),
          },
        });
      },
    });
    const record = await client.capture({
      evidenceId: "ev-1",
      caseId: "case-1",
      sourcePath: "/workloads/file.bin",
      contentSha256: digest,
      contentLength: 4,
      mediaType: "application/octet-stream",
    });
    expect(record.auditSha256).toBe("b".repeat(64));
  });

  it("creates cases, appends annotations, and batches a real device export manifest", async () => {
    const requests: Main[] = [];
    let acceptedEvidenceIds = 0;
    const client = new EvidenceClient({
      async request(request) {
        requests.push(request);
        if (request.content.case === "poisonCase") {
          return create(MainSchema, {
            commandId: request.commandId,
            commandStatus: CommandStatus.OK,
            content: {
              case: "poisonCase",
              value: create(CaseSchema, {
                ...request.content.value,
                ownerId: "session-0000000000000001",
                createdAtMs: 1n,
              }),
            },
          });
        }
        if (request.content.case === "poisonAnnotation") {
          return create(MainSchema, {
            commandId: request.commandId,
            commandStatus: CommandStatus.OK,
            content: {
              case: "poisonAnnotation",
              value: create(AnnotationSchema, {
                ...request.content.value,
                authorId: "session-0000000000000001",
                createdAtMs: 2n,
              }),
            },
          });
        }
        if (request.content.case === "poisonExportManifest") {
          acceptedEvidenceIds += request.content.value.evidenceIds.length;
          return create(MainSchema, {
            commandId: request.commandId,
            commandStatus: CommandStatus.OK,
            content: {
              case: "poisonExportManifest",
              value: create(ExportManifestSchema, {
                exportId: request.content.value.exportId,
                schema: request.content.value.schema,
                batchIndex: request.content.value.batchIndex,
                finalize: request.content.value.finalize,
                acceptedEvidenceIds,
                manifestSha256: request.content.value.finalize ? "c".repeat(64) : "",
              }),
            },
          });
        }
        throw new Error("unexpected request");
      },
    });

    const evidenceCase = await client.createCase({
      caseId: "case-1",
      name: "Incident 1",
      purpose: "Device evidence",
      retentionPolicy: "manual",
    });
    expect(evidenceCase.ownerId).toBe("session-0000000000000001");
    const annotation = await client.annotate({
      annotationId: "annotation-1",
      evidenceId: "evidence-1",
      text: "Verified",
      tags: ["verified", "rpc"],
    });
    expect(annotation.tags).toEqual(["verified", "rpc"]);

    const receipt = await client.prepareExport({
      exportId: "export-1",
      evidenceIds: Array.from({ length: 9 }, (_, index) => `evidence-${9 - index}`),
    });
    expect(receipt.manifestSha256).toBe("c".repeat(64));
    const exportRequests = requests.flatMap((request) =>
      request.content.case === "poisonExportManifest" ? [request.content.value] : []);
    expect(exportRequests).toHaveLength(2);
    expect(exportRequests[0].evidenceIds).toEqual([
      "evidence-1", "evidence-2", "evidence-3", "evidence-4",
      "evidence-5", "evidence-6", "evidence-7", "evidence-8",
    ]);
    expect(exportRequests[1].evidenceIds).toEqual(["evidence-9"]);
    expect(exportRequests[1].finalize).toBe(true);
  });
});
