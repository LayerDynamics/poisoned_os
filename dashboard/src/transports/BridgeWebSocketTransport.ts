import {
  type DiscoveredDevice,
  type Transport,
  TransportError,
  type TransportHealth,
  throwIfAborted,
} from "./Transport";

interface BridgeDevice {
  id: string;
  label: string;
  transport: "usb" | "ble";
}

interface BridgeDeviceList { devices: BridgeDevice[]; }
interface BridgeSession { session_id: string; device_id: string; }

export function selectBridgeDevice(
  devices: readonly DiscoveredDevice[],
  requestedId?: string | null,
): DiscoveredDevice | undefined {
  if (!requestedId) return devices[0];
  const selected = devices.find((device) => device.id === requestedId);
  if (!selected) throw new TransportError("io", "requested bridge device is not connected");
  return selected;
}

export function bridgeWebSocketProtocol(token: string): string {
  if (!/^[0-9a-f]{64}$/.test(token)) {
    throw new TransportError("io", "bridge token must be 64 lowercase hexadecimal characters");
  }
  return `poisoned-os.rpc.v1.${token}`;
}

export class BridgeWebSocketTransport implements Transport {
  public readonly kind = "serial" as const;
  public readonly mtu = 1024;
  private socket: WebSocket | null = null;
  private queuedBytes = 0;
  private lastError: string | undefined;
  private readonly frames: Uint8Array[] = [];
  private readonly readers: Array<(frame: Uint8Array | null) => void> = [];
  private readonly disconnectHandlers = new Set<() => void>();

  public constructor(
    private readonly baseUrl: string,
    private readonly token: string,
  ) {}

  public get health(): TransportHealth {
    return {
      connected: this.socket?.readyState === WebSocket.OPEN,
      writable: this.socket?.readyState === WebSocket.OPEN,
      queuedBytes: this.queuedBytes,
      lastError: this.lastError,
    };
  }

  public async discover(signal?: AbortSignal): Promise<readonly DiscoveredDevice[]> {
    throwIfAborted(signal);
    bridgeWebSocketProtocol(this.token);
    const response = await fetch(`${this.baseUrl}/v1/devices`, {
      headers: { "x-poison-origin-token": this.token },
      signal,
    });
    if (!response.ok) throw new TransportError("io", `bridge discovery failed (${response.status})`);
    const list = await response.json() as BridgeDeviceList;
    return list.devices.map((device) => ({
      id: device.id,
      label: `${device.label} (${device.transport.toUpperCase()})`,
      kind: "serial" as const,
      metadata: { bridge: this.baseUrl },
    }));
  }

  public async connect(device: DiscoveredDevice, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (device.kind !== "serial") throw new TransportError("io", "bridge device is not serial");
    const response = await fetch(
      `${this.baseUrl}/v1/devices/${encodeURIComponent(device.id)}/sessions`,
      {
        method: "POST",
        headers: { "x-poison-origin-token": this.token },
        signal,
      },
    );
    if (!response.ok) throw new TransportError("io", `bridge session failed (${response.status})`);
    const session = await response.json() as BridgeSession;
    const socketUrl = new URL(`${this.baseUrl}/v1/sessions/${session.session_id}/stream`);
    socketUrl.protocol = socketUrl.protocol === "https:" ? "wss:" : "ws:";
    const socket = new WebSocket(socketUrl, bridgeWebSocketProtocol(this.token));
    socket.binaryType = "arraybuffer";
    await new Promise<void>((resolve, reject) => {
      const abort = () => {
        socket.close();
        reject(new TransportError("aborted", "bridge connection aborted"));
      };
      signal?.addEventListener("abort", abort, { once: true });
      socket.onopen = () => {
        signal?.removeEventListener("abort", abort);
        if (socket.protocol !== bridgeWebSocketProtocol(this.token)) {
          socket.close();
          reject(new TransportError("io", "bridge did not authenticate the WebSocket protocol"));
          return;
        }
        resolve();
      };
      socket.onerror = () => {
        signal?.removeEventListener("abort", abort);
        reject(new TransportError("io", "bridge WebSocket connection failed"));
      };
    });
    socket.onmessage = (event) => {
      const frame = event.data instanceof ArrayBuffer ? new Uint8Array(event.data) : null;
      if (!frame) {
        this.lastError = "bridge returned a non-binary frame";
        socket.close();
        return;
      }
      const reader = this.readers.shift();
      if (reader) reader(frame); else this.frames.push(frame);
    };
    socket.onclose = () => {
      this.socket = null;
      for (const reader of this.readers.splice(0)) reader(null);
      for (const handler of this.disconnectHandlers) handler();
    };
    this.socket = socket;
  }

  public async read(signal?: AbortSignal): Promise<Uint8Array | null> {
    throwIfAborted(signal);
    const frame = this.frames.shift();
    if (frame) return frame;
    if (!this.socket) throw new TransportError("not-connected", "bridge transport is not connected");
    return new Promise<Uint8Array | null>((resolve, reject) => {
      const abort = () => reject(new TransportError("aborted", "bridge read aborted"));
      signal?.addEventListener("abort", abort, { once: true });
      this.readers.push((value) => {
        signal?.removeEventListener("abort", abort);
        resolve(value);
      });
    });
  }

  public async write(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (frame.byteLength > this.mtu) throw new TransportError("frame-too-large", "frame exceeds bridge MTU");
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
      throw new TransportError("not-connected", "bridge transport is not connected");
    }
    this.queuedBytes += frame.byteLength;
    try { this.socket.send(frame); } finally { this.queuedBytes -= frame.byteLength; }
  }

  public async close(): Promise<void> {
    this.socket?.close();
    this.socket = null;
    this.frames.length = 0;
    for (const reader of this.readers.splice(0)) reader(null);
  }

  public onDisconnect(handler: () => void): () => void {
    this.disconnectHandlers.add(handler);
    return () => this.disconnectHandlers.delete(handler);
  }
}
