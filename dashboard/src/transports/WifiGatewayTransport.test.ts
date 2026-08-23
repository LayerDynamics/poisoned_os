import { afterEach, beforeEach, describe, expect, it } from "vitest";
import {
  selectWifiBoard,
  WebRuntimeTransport,
  webRuntimeProtocol,
} from "./WifiGatewayTransport";

class TestWebSocket {
  static readonly CONNECTING = 0;
  static readonly OPEN = 1;
  static readonly CLOSING = 2;
  static readonly CLOSED = 3;
  readonly binaryType = "arraybuffer";
  readonly sent: Uint8Array[] = [];
  readonly protocol: string;
  readyState = TestWebSocket.CONNECTING;
  onopen: (() => void) | null = null;
  onerror: (() => void) | null = null;
  onmessage: ((event: { data: ArrayBuffer }) => void) | null = null;
  onclose: (() => void) | null = null;

  constructor(readonly url: string, protocol: string) {
    this.protocol = protocol;
    queueMicrotask(() => {
      this.readyState = TestWebSocket.OPEN;
      this.onopen?.();
    });
  }

  send(data: Uint8Array): void {
    this.sent.push(data.slice());
  }

  close(): void {
    this.readyState = TestWebSocket.CLOSED;
    this.onclose?.();
  }

  receive(data: Uint8Array): void {
    this.onmessage?.({ data: data.slice().buffer });
  }
}

describe("Wi-Fi gateway browser transport", () => {
  const token = "a".repeat(64);
  let originalFetch: typeof fetch;
  let originalWebSocket: typeof WebSocket;
  let sockets: TestWebSocket[];

  beforeEach(() => {
    originalFetch = globalThis.fetch;
    originalWebSocket = globalThis.WebSocket;
    sockets = [];
    globalThis.fetch = (async (input: RequestInfo | URL) => {
      expect(String(input)).toBe("http://poisoned.local:55173/api/runtime/v1/manifest");
      return new Response(JSON.stringify({
        version: 1,
        primaryRoute: "web",
        transport: {
          kind: "web",
          network: "wifi",
          discovery: { protocols: ["http", "https"], path: "/api/runtime/v1/manifest" },
          rpc: { protocols: ["ws", "wss"], pathTemplate: "/api/runtime/v1/boards/{boardId}/rpc" },
        },
        browserToken: token,
        boards: [
          { id: "field", label: "Field Flipper", state: "available" },
          { id: "lab", label: "Lab Flipper", state: "in-use" },
        ],
        services: {
          builder: { httpPath: "/api/runtime/v1/services/builder/proxy/", webSocketPath: "/api/runtime/v1/services/builder/proxy/" },
          workloads: { httpPath: "/api/runtime/v1/services/workloads/proxy/", webSocketPath: "/api/runtime/v1/services/workloads/proxy/" },
        },
      }), { status: 200, headers: { "content-type": "application/json" } });
    }) as typeof fetch;
    globalThis.WebSocket = class extends TestWebSocket {
      static readonly CONNECTING = 0;
      static readonly OPEN = 1;
      static readonly CLOSING = 2;
      static readonly CLOSED = 3;
      constructor(url: string | URL, protocols?: string | string[]) {
        super(String(url), String(protocols));
        sockets.push(this);
      }
    } as unknown as typeof WebSocket;
  });

  afterEach(() => {
    globalThis.fetch = originalFetch;
    globalThis.WebSocket = originalWebSocket;
  });

  it("discovers configured Wi-Fi boards from the current Node runtime", async () => {
    const transport = new WebRuntimeTransport("http://poisoned.local:55173");
    const devices = await transport.discover();

    expect(transport.kind).toBe("web");
    expect(devices).toEqual([
      {
        id: "field",
        label: "Field Flipper",
        kind: "web",
        metadata: { runtime: "http://poisoned.local:55173", state: "available" },
      },
      {
        id: "lab",
        label: "Lab Flipper",
        kind: "web",
        metadata: { runtime: "http://poisoned.local:55173", state: "in-use" },
      },
    ]);
    expect(transport.services).toEqual({
      builder: {
        httpUrl: "http://poisoned.local:55173/api/runtime/v1/services/builder/proxy/",
        webSocketUrl: "ws://poisoned.local:55173/api/runtime/v1/services/builder/proxy/",
      },
      workloads: {
        httpUrl: "http://poisoned.local:55173/api/runtime/v1/services/workloads/proxy/",
        webSocketUrl: "ws://poisoned.local:55173/api/runtime/v1/services/workloads/proxy/",
      },
    });
    expect(transport.browserHeaders()).toEqual({ "x-poison-runtime-browser": token });
    expect(transport.serviceWebSocketProtocol()).toBe(`poisoned-os.web-service.v1.${token}`);
    expect(selectWifiBoard(devices)?.id).toBe("field");
    expect(selectWifiBoard(devices, "lab")?.id).toBe("lab");
    expect(() => selectWifiBoard(devices, "missing")).toThrow(/not configured/i);
  });

  it("carries only opaque RPC bytes over the authenticated board socket", async () => {
    const transport = new WebRuntimeTransport("http://poisoned.local:55173");
    const [device] = await transport.discover();
    await transport.connect(device!);

    expect(sockets[0].url).toBe("ws://poisoned.local:55173/api/runtime/v1/boards/field/rpc");
    expect(sockets[0].protocol).toBe(webRuntimeProtocol(token));

    await transport.write(Uint8Array.of(0x08, 0x96, 0x01));
    expect(sockets[0].sent).toEqual([Uint8Array.of(0x08, 0x96, 0x01)]);

    const response = transport.read();
    sockets[0].receive(Uint8Array.of(0x10, 0x01));
    await expect(response).resolves.toEqual(Uint8Array.of(0x10, 0x01));
  });

  it("rejects a runtime that does not declare web as its primary route", async () => {
    globalThis.fetch = (async () => new Response(JSON.stringify({
      version: 1,
      primaryRoute: "wifi",
      transport: {
        kind: "web",
        network: "wifi",
        discovery: { protocols: ["http", "https"], path: "/api/runtime/v1/manifest" },
        rpc: { protocols: ["ws", "wss"], pathTemplate: "/api/runtime/v1/boards/{boardId}/rpc" },
      },
      browserToken: token,
      boards: [],
      services: {},
    }), { status: 200 })) as typeof fetch;
    const transport = new WebRuntimeTransport("http://poisoned.local:55173");
    await expect(transport.discover()).rejects.toThrow(/primary/i);
  });

  it("uses HTTPS discovery and WSS RPC as first-class web transports", async () => {
    globalThis.fetch = (async (input: RequestInfo | URL) => {
      expect(String(input)).toBe("https://poisoned.local:55173/api/runtime/v1/manifest");
      return new Response(JSON.stringify({
        version: 1,
        primaryRoute: "web",
        transport: {
          kind: "web",
          network: "wifi",
          discovery: { protocols: ["http", "https"], path: "/api/runtime/v1/manifest" },
          rpc: { protocols: ["ws", "wss"], pathTemplate: "/api/runtime/v1/boards/{boardId}/rpc" },
        },
        browserToken: token,
        boards: [{ id: "field", label: "Field Flipper", state: "available" }],
        services: {},
      }), { status: 200 });
    }) as typeof fetch;
    const transport = new WebRuntimeTransport("https://poisoned.local:55173");
    const [device] = await transport.discover();
    await transport.connect(device!);
    expect(sockets[0].url).toBe("wss://poisoned.local:55173/api/runtime/v1/boards/field/rpc");
  });
});
