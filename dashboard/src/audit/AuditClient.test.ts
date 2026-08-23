import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { CommandStatus, MainSchema } from "../generated/flipper_pb";
import { AuditDecision, AuditEventSchema, AuditSnapshotEndSchema } from "../generated/poison_audit_pb";
import { verifyAuditSequence, AuditClient, AuditClientError, type AuditSession } from "./AuditClient";

const digest = "00".repeat(32);
const event = (eventId: bigint, priorDigest: string, value = "11".repeat(32)) => ({ eventId, priorDigest, actorDigest: digest, action: "open", resource: "device", decision: "allowed" as const, timestampMs: 1n, correlationId: digest, safeMetadata: "source=rpc", digest: value });
const bytes = (value: number) => new Uint8Array(32).fill(value);

describe("AuditClient", () => {
  it("verifies ordered predecessor-linked events", () => {
    verifyAuditSequence([event(1n, digest)]);
    expect(() => verifyAuditSequence([event(2n, digest)])).toThrowError(new AuditClientError("gap", "audit event sequence has a gap"));
    expect(() => verifyAuditSequence([event(1n, "22".repeat(32))])).toThrowError(new AuditClientError("digest", "audit chain predecessor does not match"));
  });

  it("fetches and verifies the streamed encrypted device audit snapshot", async () => {
    const session: AuditSession = {
      async requestStream(request, maximum) {
        expect(maximum).toBe(17);
        expect(request.content.case).toBe("poisonAuditSnapshotRequest");
        return [
          create(MainSchema, {
            commandId: request.commandId, commandStatus: CommandStatus.OK, hasNext: true,
            content: { case: "poisonAuditEvent", value: create(AuditEventSchema, {
              eventId: 1n, priorDigest: bytes(0), actorDigest: bytes(3), action: "pair", resource: "client",
              decision: AuditDecision.AUDIT_ALLOWED, timestampMs: 10n, correlationId: bytes(4),
              safeMetadata: "role=operator", digest: bytes(1),
            }) },
          }),
          create(MainSchema, {
            commandId: request.commandId, commandStatus: CommandStatus.OK, hasNext: true,
            content: { case: "poisonAuditEvent", value: create(AuditEventSchema, {
              eventId: 2n, priorDigest: bytes(1), actorDigest: bytes(3), action: "status", resource: "device",
              decision: AuditDecision.AUDIT_ALLOWED, timestampMs: 11n, correlationId: bytes(5),
              safeMetadata: "source=rpc", digest: bytes(2),
            }) },
          }),
          create(MainSchema, {
            commandId: request.commandId, commandStatus: CommandStatus.OK,
            content: { case: "poisonAuditSnapshotEnd", value: create(AuditSnapshotEndSchema, {
              nextEventId: 3n, lastDigest: bytes(2), truncated: false,
            }) },
          }),
        ];
      },
    };

    await expect(new AuditClient(session).read()).resolves.toMatchObject({
      verified: true,
      truncated: false,
      nextEventId: 3n,
      lastDigest: "02".repeat(32),
      events: [
        { eventId: 1n, action: "pair", decision: "allowed" },
        { eventId: 2n, action: "status", decision: "allowed" },
      ],
    });
  });
});
