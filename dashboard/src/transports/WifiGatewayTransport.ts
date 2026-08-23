import {
  type DiscoveredDevice,
  type Transport,
  TransportError,
  type TransportHealth,
  throwIfAborted,
} from "./Transport";

interface WebRuntimeBoard {
  id: string;
  label: string;
  state: "available" | "connecting" | "in-use";
}

interface WebRuntimeManifest {
  version: 1;
  primaryRoute: "web";
  transport: {
    kind: "web";
    network: "wifi";
    discovery: { protocols: ["http", "https"]; path: string };
    rpc: { protocols: ["ws", "wss"]; pathTemplate: string };
  };
  browserToken: string;
  boards: WebRuntimeBoard[];
  services: Record<string, { httpPath: string; webSocketPath: string }>;
}

export interface WebRuntimeServiceEndpoint {
  httpUrl: string;
  webSocketUrl: string;
}

interface PendingReader {
  resolve: (frame: Uint8Array | null) => void;
  reject: (error: Error) => void;
  signal?: AbortSignal;
  abort?: () => void;
}

const TOKEN_PATTERN = /^[0-9a-f]{64}$/;

export function webRuntimeProtocol(token: string): string {
  if (!TOKEN_PATTERN.test(token)) {
    throw new TransportError("io", "Wi-Fi runtime token must be 64 lowercase hexadecimal characters");
  }
  return `poisoned-os.web-rpc.v1.${token}`;
}

export function webServiceProtocol(token: string): string {
  if (!TOKEN_PATTERN.test(token)) {
    throw new TransportError("io", "Web service token must be 64 lowercase hexadecimal characters");
  }
  return `poisoned-os.web-service.v1.${token}`;
}

function serviceEndpoint(baseUrl: string, path: string, webSocket: boolean): string {
  if (!path.startsWith("/api/runtime/v1/services/") || !path.includes("/proxy/")) {
    throw new TransportError("io", "Node runtime service path is invalid");
  }
  const endpoint = new URL(path, baseUrl);
  if (endpoint.origin !== baseUrl || endpoint.search || endpoint.hash) {
    throw new TransportError("io", "Node runtime service path escapes its web origin");
  }
  if (webSocket) endpoint.protocol = endpoint.protocol === "https:" ? "wss:" : "ws:";
  return endpoint.href;
}

export function selectWifiBoard(
  devices: readonly DiscoveredDevice[],
  requestedId?: string | null,
): DiscoveredDevice | undefined {
  if (requestedId) {
    const selected = devices.find((device) => device.id === requestedId);
    if (!selected) throw new TransportError("io", "requested Wi-Fi board is not configured");
    return selected;
  }
  return devices.find((device) => device.metadata.state === "available");
}

function validateManifest(value: unknown): WebRuntimeManifest {
  if (!value || typeof value !== "object") throw new TransportError("io", "Wi-Fi runtime manifest is invalid");
  const manifest = value as Partial<WebRuntimeManifest>;
  if (manifest.version !== 1) throw new TransportError("io", "Wi-Fi runtime version is unsupported");
  if (manifest.primaryRoute !== "web") {
    throw new TransportError("io", "Node runtime does not declare web as its primary route");
  }
  const transport = manifest.transport;
  if (transport?.kind !== "web" || transport.network !== "wifi" ||
      transport.discovery?.path !== "/api/runtime/v1/manifest" ||
      transport.rpc?.pathTemplate !== "/api/runtime/v1/boards/{boardId}/rpc" ||
      transport.discovery.protocols?.join(",") !== "http,https" ||
      transport.rpc.protocols?.join(",") !== "ws,wss") {
    throw new TransportError("io", "Node runtime web transport declaration is invalid");
  }
  webRuntimeProtocol(manifest.browserToken ?? "");
  if (!Array.isArray(manifest.boards) || !manifest.boards.every((board) =>
    board && typeof board.id === "string" && typeof board.label === "string" &&
    ["available", "connecting", "in-use"].includes(board.state))) {
    throw new TransportError("io", "Wi-Fi runtime board inventory is invalid");
  }
  if (!manifest.services || typeof manifest.services !== "object" || Array.isArray(manifest.services) ||
      Object.values(manifest.services).some((service) => !service || typeof service !== "object" ||
        typeof service.httpPath !== "string" || typeof service.webSocketPath !== "string")) {
    throw new TransportError("io", "Wi-Fi runtime service inventory is invalid");
  }
  return manifest as WebRuntimeManifest;
}

export class WebRuntimeTransport implements Transport {
  readonly kind = "web" as const;
  readonly mtu = 1024;
  services: Readonly<Record<string, WebRuntimeServiceEndpoint>> = {};

  private socket: WebSocket | null = null;
  private token: string | null = null;
  private queuedBytes = 0;
  private lastError: string | undefined;
  private readonly frames: Uint8Array[] = [];
  private readonly readers: PendingReader[] = [];
  private readonly disconnectHandlers = new Set<() => void>();
  private readonly baseUrl: string;

  constructor(baseUrl: string = window.location.origin) {
    const normalized = new URL(baseUrl);
    if (normalized.protocol !== "http:" && normalized.protocol !== "https:") {
      throw new TransportError("io", "Wi-Fi runtime URL must use HTTP or HTTPS");
    }
    this.baseUrl = normalized.origin;
  }

  get health(): TransportHealth {
    return {
      connected: this.socket?.readyState === WebSocket.OPEN,
      writable: this.socket?.readyState === WebSocket.OPEN,
      queuedBytes: this.queuedBytes,
      lastError: this.lastError,
    };
  }

  async discover(signal?: AbortSignal): Promise<readonly DiscoveredDevice[]> {
    throwIfAborted(signal);
    const endpoint = new URL("/api/runtime/v1/manifest", this.baseUrl);
    const response = await fetch(endpoint, { signal, credentials: "same-origin" });
    if (!response.ok) {
      throw new TransportError("io", `Wi-Fi runtime discovery failed (${response.status})`);
    }
    const manifest = validateManifest(await response.json());
    this.token = manifest.browserToken;
    this.services = Object.freeze(Object.fromEntries(Object.entries(manifest.services).map(([name, service]) => [
      name,
      Object.freeze({
        httpUrl: serviceEndpoint(this.baseUrl, service.httpPath, false),
        webSocketUrl: serviceEndpoint(this.baseUrl, service.webSocketPath, true),
      }),
    ])));
    return manifest.boards.map((board) => ({
      id: board.id,
      label: board.label,
      kind: "web" as const,
      metadata: { runtime: this.baseUrl, state: board.state },
    }));
  }

  browserHeaders(): Readonly<Record<string, string>> {
    if (!this.token) throw new TransportError("not-connected", "discover the web runtime before using Node services");
    return Object.freeze({ "x-poison-runtime-browser": this.token });
  }

  serviceWebSocketProtocol(): string {
    if (!this.token) throw new TransportError("not-connected", "discover the web runtime before using Node services");
    return webServiceProtocol(this.token);
  }

  async connect(device: DiscoveredDevice, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (device.kind !== "web") throw new TransportError("io", "selected device is not a web-runtime board");
    if (!this.token) throw new TransportError("io", "discover the Wi-Fi runtime before connecting");
    if (device.metadata.state !== "available") {
      throw new TransportError("io", `Wi-Fi board ${device.id} is ${device.metadata.state ?? "unavailable"}`);
    }

    const endpoint = new URL(
      `/api/runtime/v1/boards/${encodeURIComponent(device.id)}/rpc`,
      this.baseUrl,
    );
    endpoint.protocol = endpoint.protocol === "https:" ? "wss:" : "ws:";
    const protocol = webRuntimeProtocol(this.token);
    const socket = new WebSocket(endpoint, protocol);
    socket.binaryType = "arraybuffer";

    await new Promise<void>((resolve, reject) => {
      const abort = () => {
        socket.close();
        reject(new TransportError("aborted", "Wi-Fi connection aborted"));
      };
      signal?.addEventListener("abort", abort, { once: true });
      socket.onopen = () => {
        signal?.removeEventListener("abort", abort);
        if (socket.protocol !== protocol) {
          socket.close();
          reject(new TransportError("io", "Wi-Fi runtime did not authenticate the WebSocket"));
          return;
        }
        resolve();
      };
      socket.onerror = () => {
        signal?.removeEventListener("abort", abort);
        reject(new TransportError("io", "Wi-Fi runtime connection failed"));
      };
    });

    socket.onmessage = (event) => {
      if (!(event.data instanceof ArrayBuffer)) {
        this.lastError = "Wi-Fi runtime returned a non-binary RPC frame";
        socket.close();
        return;
      }
      const frame = new Uint8Array(event.data);
      const reader = this.readers.shift();
      if (reader) {
        this.finishReader(reader, frame);
      } else {
        this.frames.push(frame);
      }
    };
    socket.onclose = () => {
      if (this.socket === socket) this.socket = null;
      for (const reader of this.readers.splice(0)) this.finishReader(reader, null);
      for (const handler of this.disconnectHandlers) handler();
    };
    this.socket = socket;
  }

  read(signal?: AbortSignal): Promise<Uint8Array | null> {
    throwIfAborted(signal);
    const frame = this.frames.shift();
    if (frame) return Promise.resolve(frame);
    if (!this.socket) return Promise.reject(new TransportError("not-connected", "Wi-Fi transport is not connected"));

    return new Promise((resolve, reject) => {
      const reader: PendingReader = { resolve, reject, signal };
      const abort = () => {
        const index = this.readers.indexOf(reader);
        if (index >= 0) this.readers.splice(index, 1);
        reject(new TransportError("aborted", "Wi-Fi read aborted"));
      };
      reader.abort = abort;
      signal?.addEventListener("abort", abort, { once: true });
      this.readers.push(reader);
    });
  }

  async write(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (frame.byteLength > this.mtu) {
      throw new TransportError("frame-too-large", "frame exceeds Wi-Fi runtime MTU");
    }
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
      throw new TransportError("not-connected", "Wi-Fi transport is not connected");
    }
    this.queuedBytes += frame.byteLength;
    try {
      this.socket.send(frame);
    } finally {
      this.queuedBytes -= frame.byteLength;
    }
  }

  async close(): Promise<void> {
    this.socket?.close();
    this.socket = null;
    this.frames.length = 0;
    for (const reader of this.readers.splice(0)) this.finishReader(reader, null);
  }

  onDisconnect(handler: () => void): () => void {
    this.disconnectHandlers.add(handler);
    return () => this.disconnectHandlers.delete(handler);
  }

  private finishReader(reader: PendingReader, frame: Uint8Array | null): void {
    if (reader.abort) reader.signal?.removeEventListener("abort", reader.abort);
    reader.resolve(frame);
  }
}
