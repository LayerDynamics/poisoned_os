export type TransportKind = "web" | "usb" | "serial" | "bluetooth";

export interface DiscoveredDevice {
  readonly id: string;
  readonly label: string;
  readonly kind: TransportKind;
  readonly metadata: Readonly<Record<string, string>>;
}

export interface TransportHealth {
  readonly connected: boolean;
  readonly writable: boolean;
  readonly queuedBytes: number;
  readonly lastError?: string;
}

export interface Transport {
  readonly kind: TransportKind;
  readonly mtu: number;
  readonly health: TransportHealth;
  discover(signal?: AbortSignal): Promise<readonly DiscoveredDevice[]>;
  connect(device: DiscoveredDevice, signal?: AbortSignal): Promise<void>;
  read(signal?: AbortSignal): Promise<Uint8Array | null>;
  write(frame: Uint8Array, signal?: AbortSignal): Promise<void>;
  close(): Promise<void>;
  onDisconnect?(handler: () => void): () => void;
}

export class TransportError extends Error {
  public constructor(
    public readonly code: "unsupported" | "not-connected" | "aborted" | "frame-too-large" | "io",
    message: string,
  ) {
    super(message);
    this.name = "TransportError";
  }
}

export function throwIfAborted(signal?: AbortSignal): void {
  if (signal?.aborted) throw new TransportError("aborted", "transport operation aborted");
}
