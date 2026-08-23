import { create } from "@bufbuild/protobuf";
import { appendAppEvent, type AppEvent } from "./AppEvents";
import { StartRequestSchema } from "../generated/application_pb";
import type { Main } from "../generated/flipper_pb";
import { CommandStatus, MainSchema } from "../generated/flipper_pb";
import { AppCommandSchema, type AppEvent as WireAppEvent } from "../generated/poison_app_pb";

const APP_PROTOCOL_VERSION = 1;
const APP_CHUNK_BYTES = 384;
const MAX_APP_CHUNKS = 33;
const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder("utf-8", { fatal: true });

interface PendingEvent {
  readonly key: string;
  readonly count: number;
  next: number;
  readonly event: WireAppEvent;
  readonly message: Uint8Array[];
  readonly schema: Uint8Array[];
  readonly rows: Uint8Array[];
}

function join(parts: readonly Uint8Array[]): string {
  const bytes = new Uint8Array(parts.reduce((total, part) => total + part.byteLength, 0));
  let offset = 0;
  for (const part of parts) { bytes.set(part, offset); offset += part.byteLength; }
  return textDecoder.decode(bytes);
}

export interface StructuredAppSession {
  request(request: Main, signal?: AbortSignal): Promise<Main>;
  onNotification(handler: (message: Main) => void): () => void;
}

export interface StructuredRun { readonly runId: string; readonly parameter: number; readonly events: readonly AppEvent[]; readonly cancelled: boolean; }

export class StructuredAppClient {
  private run: StructuredRun | null = null;
  private nextCommandId = 1;
  private readonly unsubscribe?: () => void;
  private pendingEvent: PendingEvent | null = null;
  public constructor(
    private readonly session?: StructuredAppSession,
    private readonly now: () => number = Date.now,
    private readonly onChange?: (run: StructuredRun) => void,
  ) {
    this.unsubscribe = session?.onNotification((message) => this.notification(message));
  }
  public start(runId: string, parameter: number): StructuredRun {
    if (this.run && !this.run.cancelled) throw new Error("app is already running");
    if (!runId || !Number.isInteger(parameter) || parameter < 0 || parameter > 100) throw new Error("invalid structured app request");
    this.run = { runId, parameter, events: [], cancelled: false };
    this.onChange?.(this.run);
    return this.run;
  }
  public event(event: AppEvent): StructuredRun {
    if (!this.run || this.run.cancelled) throw new Error("app run is not active");
    if (event.runId !== this.run.runId) throw new Error("app event run does not match the active run");
    this.run = { ...this.run, events: appendAppEvent(this.run.events, event) };
    this.onChange?.(this.run);
    return this.run;
  }
  public cancel(): StructuredRun { if (!this.run) throw new Error("app run is not active"); this.run = { ...this.run, cancelled: true }; this.onChange?.(this.run); return this.run; }
  public get current(): StructuredRun | null { return this.run; }

  public async launchApplication(name: string, args = "", signal?: AbortSignal): Promise<void> {
    if (!this.session || !name || textEncoder.encode(name).byteLength > 512 || textEncoder.encode(args).byteLength > 512 ||
        name.includes("\0") || args.includes("\0")) throw new Error("invalid application launch request");
    const response = await this.session.request(create(MainSchema, {
      commandId: this.nextCommandId++,
      content: { case: "appStartRequest", value: create(StartRequestSchema, { name, args }) },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "empty") {
      throw new Error("device rejected structured application launch");
    }
  }

  public async command(appId: string, commandId: string, payloadJson: string, cancel = false, signal?: AbortSignal): Promise<void> {
    if (!this.session || !this.run || this.run.cancelled) throw new Error("app run is not active");
    const appBytes = textEncoder.encode(appId);
    const runBytes = textEncoder.encode(this.run.runId);
    const commandBytes = textEncoder.encode(commandId);
    const payload = textEncoder.encode(payloadJson);
    if (appBytes.byteLength === 0 || appBytes.byteLength > 64 || runBytes.byteLength === 0 || runBytes.byteLength > 64 ||
        commandBytes.byteLength === 0 || commandBytes.byteLength > 64 || payload.byteLength > 4096 || payload.includes(0)) throw new Error("invalid structured app command");
    const chunkCount = payload.byteLength === 0 ? 0 : Math.ceil(payload.byteLength / APP_CHUNK_BYTES);
    const sends = Math.max(1, chunkCount);
    for (let chunkIndex = 0; chunkIndex < sends; chunkIndex += 1) {
      const request = create(MainSchema, {
        commandId: this.nextCommandId++,
        content: {
          case: "poisonAppCommand",
          value: create(AppCommandSchema, {
            appId,
            runId: this.run.runId,
            commandId,
            payloadJson: chunkCount === 0 ? payloadJson : "",
            cancel,
            protocolVersion: APP_PROTOCOL_VERSION,
            chunkIndex,
            chunkCount,
            payloadChunk: chunkCount === 0 ? new Uint8Array() : payload.slice(chunkIndex * APP_CHUNK_BYTES, (chunkIndex + 1) * APP_CHUNK_BYTES),
          }),
        },
      });
      const response = await this.session.request(request, signal);
      if (response.commandStatus !== CommandStatus.OK || response.content.case !== "empty") throw new Error("device rejected structured app command");
    }
    if (cancel) this.cancel();
  }

  public dispose(): void { this.unsubscribe?.(); }

  private notification(message: Main): void {
    if (message.commandId !== 0 || message.commandStatus !== CommandStatus.OK || message.content.case !== "poisonAppEvent") return;
    const wire = message.content.value;
    if (wire.chunkCount === 0) {
      if (wire.chunkIndex !== 0 || wire.messageChunk.byteLength || wire.schemaJsonChunk.byteLength || wire.rowsJsonChunk.byteLength) throw new Error("invalid unchunked app event");
      this.event(this.decodeEvent(wire));
      return;
    }
    if (wire.chunkCount > MAX_APP_CHUNKS || wire.chunkIndex >= wire.chunkCount ||
        wire.messageChunk.byteLength > APP_CHUNK_BYTES || wire.schemaJsonChunk.byteLength > APP_CHUNK_BYTES ||
        wire.rowsJsonChunk.byteLength > APP_CHUNK_BYTES) throw new Error("invalid structured app event chunk");
    this.validateChunkFields(wire);
    const key = `${wire.appId}\0${wire.runId}\0${wire.eventId}\0${wire.sequence}\0${wire.kind.case}`;
    if (wire.chunkIndex === 0) {
      this.pendingEvent = { key, count: wire.chunkCount, next: 0, event: wire, message: [], schema: [], rows: [] };
    }
    const pending = this.pendingEvent;
    if (!pending || pending.key !== key || pending.count !== wire.chunkCount || pending.next !== wire.chunkIndex) {
      this.pendingEvent = null;
      throw new Error("structured app event chunk gap");
    }
    if (wire.messageChunk.byteLength) pending.message.push(wire.messageChunk);
    if (wire.schemaJsonChunk.byteLength) pending.schema.push(wire.schemaJsonChunk);
    if (wire.rowsJsonChunk.byteLength) pending.rows.push(wire.rowsJsonChunk);
    pending.next += 1;
    if (pending.next === pending.count) {
      this.pendingEvent = null;
      this.event(this.decodeEvent(pending.event, {
        message: join(pending.message),
        schema: join(pending.schema),
        rows: join(pending.rows),
      }));
    }
  }

  private validateChunkFields(event: WireAppEvent): void {
    const message = event.messageChunk.byteLength;
    const schema = event.schemaJsonChunk.byteLength;
    const rows = event.rowsJsonChunk.byteLength;
    switch (event.kind.case) {
      case "log":
      case "result":
        if (!message || schema || rows) throw new Error("invalid structured app message chunk");
        return;
      case "form":
        if (message || !schema || rows) throw new Error("invalid structured app form chunk");
        return;
      case "table":
        if (message || (schema === 0) === (rows === 0)) throw new Error("invalid structured app table chunk");
        return;
      case "progress":
      case "artifact":
        if (event.chunkCount !== 1 || event.chunkIndex !== 0 || message || schema || rows) throw new Error("invalid structured app scalar chunk");
        return;
      default:
        throw new Error("structured app event kind is missing");
    }
  }

  private decodeEvent(event: WireAppEvent, chunks?: { message: string; schema: string; rows: string }): AppEvent {
    if (event.protocolVersion !== APP_PROTOCOL_VERSION || !event.appId || textEncoder.encode(event.appId).byteLength > 64 ||
        !event.runId || textEncoder.encode(event.runId).byteLength > 64 || !event.eventId ||
        textEncoder.encode(event.eventId).byteLength > 64) throw new Error("invalid structured app event identity");
    const base = { runId: event.runId, eventId: event.eventId, sequence: event.sequence, timeMs: this.now(), artifactIds: [] as readonly string[] };
    switch (event.kind.case) {
      case "log":
        return { ...base, kind: "log", level: event.kind.value.level === "error" ? "error" : event.kind.value.level === "warning" ? "warning" : "info", message: chunks?.message ?? event.kind.value.message };
      case "progress":
        return { ...base, kind: "progress", level: "info", message: event.kind.value.label, progressPercent: event.kind.value.percent };
      case "form":
        return { ...base, kind: "form", level: "info", message: "Input requested", schemaJson: chunks?.schema ?? event.kind.value.schemaJson };
      case "table":
        return { ...base, kind: "table", level: "info", message: "Table updated", schemaJson: chunks?.schema ?? event.kind.value.schemaJson, rowsJson: chunks?.rows ?? event.kind.value.rowsJson };
      case "artifact":
        return { ...base, kind: "artifact", level: "info", message: event.kind.value.name, artifactIds: [`sha256:${event.kind.value.sha256}`] };
      case "result":
        return { ...base, kind: "result", level: event.kind.value.success ? "info" : "error", message: chunks?.message ?? event.kind.value.message };
      default:
        throw new Error("structured app event kind is missing");
    }
  }
}
