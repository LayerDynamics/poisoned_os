export type AppEventLevel = "info" | "warning" | "error";
export type AppEventKind = "status" | "progress" | "log" | "warning" | "form" | "table" | "result" | "artifact" | "exit";
export interface AppEvent {
  runId: string;
  eventId?: string;
  sequence: bigint;
  timeMs: number;
  level: AppEventLevel;
  kind: AppEventKind;
  message: string;
  artifactIds: readonly string[];
  progressPercent?: number;
  schemaJson?: string;
  rowsJson?: string;
}

export class AppEventError extends Error { public constructor(public readonly code: "gap" | "invalid" | "artifact", message: string) { super(message); this.name = "AppEventError"; } }

const encoder = new TextEncoder();

export function appendAppEvent(events: readonly AppEvent[], event: AppEvent): readonly AppEvent[] {
  const previous = events.at(-1);
  if (!event.runId || encoder.encode(event.runId).byteLength > 64 || !Number.isSafeInteger(event.timeMs) || event.timeMs < 0 || encoder.encode(event.message).byteLength > 1024) throw new AppEventError("invalid", "invalid app event");
  if (event.eventId !== undefined && (!event.eventId || encoder.encode(event.eventId).byteLength > 64)) throw new AppEventError("invalid", "invalid app event id");
  if (event.progressPercent !== undefined && (!Number.isInteger(event.progressPercent) || event.progressPercent < 0 || event.progressPercent > 100)) throw new AppEventError("invalid", "invalid app progress");
  if ((event.schemaJson && encoder.encode(event.schemaJson).byteLength > 4096) || (event.rowsJson && encoder.encode(event.rowsJson).byteLength > 8192)) throw new AppEventError("invalid", "structured app payload is too large");
  if (!previous && event.sequence !== 0n) throw new AppEventError("gap", "app event sequence must start at zero");
  if (previous && (previous.runId !== event.runId || event.sequence !== previous.sequence + 1n)) throw new AppEventError("gap", "app event sequence gap");
  if (event.artifactIds.some((id) => !/^sha256:[0-9a-f]{64}$/.test(id))) throw new AppEventError("artifact", "invalid artifact digest");
  return [...events, event];
}
