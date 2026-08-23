import { create } from "@bufbuild/protobuf";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { AuditDecision as WireAuditDecision, AuditSnapshotRequestSchema } from "../generated/poison_audit_pb";

export type AuditDecision = "denied" | "allowed" | "expired" | "revoked";

export interface AuditEvent {
  eventId: bigint;
  priorDigest: string;
  actorDigest: string;
  action: string;
  resource: string;
  decision: AuditDecision;
  timestampMs: bigint;
  correlationId: string;
  safeMetadata: string;
  digest: string;
}

export interface AuditSnapshot {
  readonly events: readonly AuditEvent[];
  readonly truncated: boolean;
  readonly verified: boolean;
  readonly nextEventId: bigint;
  readonly lastDigest: string;
}

export interface AuditSession {
  requestStream(request: Main, maxResponses?: number, signal?: AbortSignal): Promise<readonly Main[]>;
}

export class AuditClientError extends Error {
  public constructor(public readonly code: "gap" | "digest" | "invalid", message: string) { super(message); this.name = "AuditClientError"; }
}

export function verifyAuditSequence(events: readonly AuditEvent[], initialDigest = "00".repeat(32), initialEventId = 1n): void {
  let expectedId = initialEventId;
  let previousDigest = initialDigest;
  for (const event of events) {
    if (event.eventId !== expectedId) throw new AuditClientError("gap", "audit event sequence has a gap");
    if (!isDigest(event.priorDigest) || !isDigest(event.digest) || !isDigest(event.actorDigest) || !isDigest(event.correlationId)) throw new AuditClientError("invalid", "audit digest is malformed");
    if (event.priorDigest !== previousDigest) throw new AuditClientError("digest", "audit chain predecessor does not match");
    if (!printable(event.action, 32) || !printable(event.resource, 32) || !printable(event.safeMetadata, 64) || event.timestampMs < 0n) {
      throw new AuditClientError("invalid", "audit event fields are malformed");
    }
    expectedId += 1n;
    previousDigest = event.digest;
  }
}

function isDigest(value: string): boolean { return /^[0-9a-f]{64}$/.test(value); }
function printable(value: string, maximum: number): boolean {
  return value.length > 0 && value.length <= maximum && /^[\x20-\x7e]+$/.test(value);
}
function hex(value: Uint8Array): string {
  if (value.byteLength !== 32) throw new AuditClientError("invalid", "audit digest is malformed");
  return Array.from(value, (octet) => octet.toString(16).padStart(2, "0")).join("");
}

const decisions: Readonly<Record<number, AuditDecision>> = {
  [WireAuditDecision.AUDIT_DENIED]: "denied",
  [WireAuditDecision.AUDIT_ALLOWED]: "allowed",
  [WireAuditDecision.AUDIT_EXPIRED]: "expired",
  [WireAuditDecision.AUDIT_REVOKED]: "revoked",
};

export class AuditClient {
  private nextCommandId = 40_000;
  public constructor(private readonly session: AuditSession) {}

  public async read(afterEventId = 0n, maxEvents = 16, signal?: AbortSignal): Promise<AuditSnapshot> {
    if (afterEventId < 0n || !Number.isInteger(maxEvents) || maxEvents < 1 || maxEvents > 16) {
      throw new AuditClientError("invalid", "audit snapshot request is invalid");
    }
    const responses = await this.session.requestStream(create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonAuditSnapshotRequest",
        value: create(AuditSnapshotRequestSchema, { afterEventId, maxEvents }),
      },
    }), maxEvents + 1, signal);
    const events: AuditEvent[] = [];
    let end: Extract<Main["content"], { case: "poisonAuditSnapshotEnd" }>["value"] | null = null;
    for (const [index, response] of responses.entries()) {
      if (response.commandStatus !== CommandStatus.OK) throw new AuditClientError("invalid", "device rejected audit snapshot");
      if (response.content.case === "poisonAuditEvent" && response.hasNext && !end) {
        const event = response.content.value;
        const decision = decisions[event.decision];
        if (!decision) throw new AuditClientError("invalid", "device returned an invalid audit decision");
        events.push({
          eventId: event.eventId,
          priorDigest: hex(event.priorDigest),
          actorDigest: hex(event.actorDigest),
          action: event.action,
          resource: event.resource,
          decision,
          timestampMs: event.timestampMs,
          correlationId: hex(event.correlationId),
          safeMetadata: event.safeMetadata,
          digest: hex(event.digest),
        });
      } else if (response.content.case === "poisonAuditSnapshotEnd" && !response.hasNext && index === responses.length - 1) {
        end = response.content.value;
      } else {
        throw new AuditClientError("invalid", "device returned a malformed audit stream");
      }
    }
    if (!end) throw new AuditClientError("invalid", "device omitted the audit snapshot receipt");
    const lastDigest = hex(end.lastDigest);
    const initialEventId = end.truncated && events.length ? events[0].eventId : afterEventId + 1n;
    const initialDigest = end.truncated && events.length ? events[0].priorDigest : "00".repeat(32);
    verifyAuditSequence(events, initialDigest, initialEventId);
    if (events.length && end.nextEventId <= events.at(-1)!.eventId) {
      throw new AuditClientError("gap", "audit receipt does not advance beyond returned events");
    }
    if (!end.truncated) {
      const expectedLastDigest = events.at(-1)?.digest ?? "00".repeat(32);
      if (lastDigest !== expectedLastDigest || end.nextEventId !== (events.at(-1)?.eventId ?? 0n) + 1n) {
        throw new AuditClientError("digest", "audit snapshot receipt does not match the full chain");
      }
    }
    return {
      events,
      truncated: end.truncated,
      verified: !end.truncated && afterEventId === 0n,
      nextEventId: end.nextEventId,
      lastDigest,
    };
  }
}
