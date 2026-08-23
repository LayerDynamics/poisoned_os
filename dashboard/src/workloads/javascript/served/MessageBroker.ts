export type BrokerCapability = "device.status.read" | "device.app.run" | "evidence.create";

export interface BrokerRequest {
  readonly type: "poison.request";
  readonly nonce: string;
  readonly sequence: number;
  readonly capability: BrokerCapability;
  readonly operation: "read" | "run" | "create";
  readonly payload: unknown;
}

export interface BrokerResponse {
  readonly type: "poison.response";
  readonly nonce: string;
  readonly sequence: number;
  readonly ok: boolean;
  readonly result?: unknown;
  readonly error?: string;
}

const operations: Readonly<Record<BrokerCapability, BrokerRequest["operation"]>> = {
  "device.status.read": "read",
  "device.app.run": "run",
  "evidence.create": "create",
};
const requestKeys = ["capability", "nonce", "operation", "payload", "sequence", "type"] as const;

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function payloadIsBounded(value: unknown): boolean {
  try {
    return new TextEncoder().encode(JSON.stringify(value)).byteLength <= 16 * 1024;
  } catch {
    return false;
  }
}

export class MessageBroker {
  private nextSequence = 0;
  private active = true;
  private source: WindowProxy | null = null;

  public constructor(
    private readonly nonce: string,
    private readonly capabilities: ReadonlySet<BrokerCapability>,
  ) {}

  public bind(source: WindowProxy): void {
    if (!this.active || this.source) throw new Error("broker source is already bound");
    this.source = source;
  }

  public acceptsEnvelope(event: Pick<MessageEvent, "origin" | "source">): boolean {
    return this.active && this.source !== null && event.source === this.source && event.origin === "null";
  }

  public accept(data: unknown): BrokerRequest {
    if (!this.active || !this.source || !isObject(data) || data.type !== "poison.request" ||
        Object.keys(data).length !== requestKeys.length ||
        !requestKeys.every((key) => Object.prototype.hasOwnProperty.call(data, key)) ||
        data.nonce !== this.nonce || data.sequence !== this.nextSequence ||
        !Number.isSafeInteger(data.sequence) || typeof data.capability !== "string" ||
        typeof data.operation !== "string" || !(data.capability in operations) ||
        !this.capabilities.has(data.capability as BrokerCapability) ||
        operations[data.capability as BrokerCapability] !== data.operation ||
        !payloadIsBounded(data.payload)) {
      throw new Error("broker replay, forgery, or capability denial");
    }
    this.nextSequence += 1;
    return data as unknown as BrokerRequest;
  }

  public reply(
    request: BrokerRequest,
    response: Omit<BrokerResponse, "type" | "nonce" | "sequence">,
  ): void {
    if (!this.active || !this.source) throw new Error("broker is closed");
    this.source.postMessage({
      type: "poison.response",
      nonce: this.nonce,
      sequence: request.sequence,
      ...response,
    } satisfies BrokerResponse, "*");
  }

  public close(): void {
    this.active = false;
    this.source = null;
    this.nextSequence = 0;
  }
}

export function createBrokerNonce(): string {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return [...bytes].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}
