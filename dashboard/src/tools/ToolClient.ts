import { create } from "@bufbuild/protobuf";
import { StructuredAppClient, type StructuredAppSession } from "../apps/StructuredAppClient";
import type { AppEvent } from "../apps/AppEvents";
import { CommandStatus, MainSchema } from "../generated/flipper_pb";
import { ToolRunSchema } from "../generated/poison_evidence_pb";
import { CancelRequestSchema } from "../generated/poison_session_pb";

const TOOLS_APP_ID = "org.poison.tools";
const encoder = new TextEncoder();

export interface ToolRunState {
  readonly toolId: string;
  readonly runId: string;
  readonly events: readonly AppEvent[];
  readonly active: boolean;
}

export class ToolClient {
  private readonly structured: StructuredAppClient;
  private readonly listeners = new Set<(state: ToolRunState) => void>();
  private nextCommandId = 20_000;
  private activeStartCommandId: number | null = null;
  private state: ToolRunState | null = null;

  public constructor(private readonly session: StructuredAppSession) {
    this.structured = new StructuredAppClient(session, Date.now, (run) => {
      if (!this.state || this.state.runId !== run.runId) return;
      this.state = { ...this.state, events: run.events };
      this.emit();
    });
  }

  public get current(): ToolRunState | null { return this.state; }

  public subscribe(listener: (state: ToolRunState) => void): () => void {
    this.listeners.add(listener);
    if (this.state) listener(this.state);
    return () => this.listeners.delete(listener);
  }

  public async start(toolId: string, runId: string, signal?: AbortSignal): Promise<ToolRunState> {
    if (this.state?.active) throw new Error("a tool run is already active");
    this.validateIdentifier(toolId, "tool id");
    this.validateIdentifier(runId, "run id");
    const startCommandId = this.nextCommandId++;
    const response = await this.session.request(create(MainSchema, {
      commandId: startCommandId,
      content: {
        case: "poisonToolRun",
        value: create(ToolRunSchema, {
          toolId,
          runId,
          caseId: "local",
          toolVersion: "builtin",
          state: "start",
          startedAtMs: BigInt(Date.now()),
        }),
      },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "empty") {
      throw new Error("device rejected the tool run");
    }
    this.structured.start(runId, 0);
    this.activeStartCommandId = startCommandId;
    this.state = { toolId, runId, events: [], active: true };
    this.emit();
    return this.state;
  }

  public async command(commandId: string, payload: unknown, signal?: AbortSignal): Promise<ToolRunState> {
    if (!this.state?.active) throw new Error("tool run is not active");
    this.validateIdentifier(commandId, "command id");
    const payloadJson = JSON.stringify(payload);
    if (encoder.encode(payloadJson).byteLength > 4096) throw new Error("tool payload is too large");
    await this.structured.command(TOOLS_APP_ID, commandId, payloadJson, false, signal);
    const structured = this.structured.current;
    if (!structured || structured.runId !== this.state.runId) throw new Error("tool event stream was lost");
    this.state = { ...this.state, events: structured.events };
    this.emit();
    return this.state;
  }

  public async stop(signal?: AbortSignal): Promise<ToolRunState> {
    if (!this.state?.active) throw new Error("tool run is not active");
    if (this.activeStartCommandId === null) throw new Error("tool cancellation target is unavailable");
    const response = await this.session.request(create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonCancelRequest",
        value: create(CancelRequestSchema, {
          commandId: BigInt(this.activeStartCommandId),
          reason: "user-requested",
        }),
      },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonCancelled" ||
        !response.content.value.accepted || response.content.value.commandId !== BigInt(this.activeStartCommandId)) {
      throw new Error("device rejected the tool stop request");
    }
    this.activeStartCommandId = null;
    this.structured.cancel();
    this.state = { ...this.state, active: false };
    this.emit();
    return this.state;
  }

  public dispose(): void { this.structured.dispose(); }

  private emit(): void {
    if (!this.state) return;
    for (const listener of this.listeners) listener(this.state);
  }

  private validateIdentifier(value: string, label: string): void {
    if (!/^[a-z0-9._-]{1,64}$/.test(value)) throw new Error(`${label} is invalid`);
  }
}
