import { afterEach, describe, expect, it } from "vitest";
import { get as httpsGet } from "node:https";
import { createServer as createHttpServer, type Server as HttpServer } from "node:http";
import WebSocket, { WebSocketServer } from "ws";
import { createRuntimeServer, type RuntimeServer } from "./runtime-server";
import { ServiceAddressRegistry } from "./service-address-registry";
import type { RuntimeConfig } from "./runtime-config";
import type { WifiRpcSession } from "./wifi-control-plane";

class TestSession implements WifiRpcSession {
  readonly sent: Uint8Array[] = [];
  closed = false;
  private readonly listeners = new Set<(data: Uint8Array) => void>();
  private readonly disconnectListeners = new Set<(error: Error) => void>();

  constructor(readonly boardId: string, readonly clientId: string) {}

  async send(data: Uint8Array): Promise<void> {
    this.sent.push(data.slice());
  }

  onData(listener: (data: Uint8Array) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  onDisconnect(listener: (error: Error) => void): () => void {
    this.disconnectListeners.add(listener);
    return () => this.disconnectListeners.delete(listener);
  }

  async close(): Promise<void> {
    this.closed = true;
  }

  receive(data: Uint8Array): void {
    for (const listener of this.listeners) listener(data);
  }

  disconnect(error: Error): void {
    for (const listener of this.disconnectListeners) listener(error);
  }
}

class TestControlPlane {
  readonly sessions: TestSession[] = [];
  inUse = false;

  statuses() {
    return [{ id: "field", label: "Field Flipper", state: this.inUse ? "in-use" as const : "available" as const }];
  }

  async openSession(boardId: string, clientId: string): Promise<TestSession> {
    if (boardId !== "field") throw new Error(`Unknown Wi-Fi board ${boardId}`);
    if (this.inUse) throw new Error("Wi-Fi board field is already in use");
    this.inUse = true;
    const session = new TestSession(boardId, clientId);
    const close = session.close.bind(session);
    session.close = async () => {
      await close();
      this.inUse = false;
    };
    this.sessions.push(session);
    return session;
  }
}

const browserToken = "a".repeat(64);
const adminToken = "b".repeat(64);
const testCertificate = `-----BEGIN CERTIFICATE-----
MIIBmDCCAT+gAwIBAgIUP0TqjnYCWP398O1YuakEV/5IJxgwCgYIKoZIzj0EAwIw
FDESMBAGA1UEAwwJMTI3LjAuMC4xMB4XDTI2MDgyMjIyNTk0N1oXDTM2MDgxOTIy
NTk0N1owFDESMBAGA1UEAwwJMTI3LjAuMC4xMFkwEwYHKoZIzj0CAQYIKoZIzj0D
AQcDQgAEoAlEW6g9XEm0+tUj4jbkiNG2FcBLxA+e5dU4s+ZJcInBhoO7Z8XU8Sh1
LtsMDnnQDoDFZ+TsxWBpHOMk9usgrqNvMG0wHQYDVR0OBBYEFBaAIzVAZ7dng3fE
e4gpqFJ273LRMB8GA1UdIwQYMBaAFBaAIzVAZ7dng3fEe4gpqFJ273LRMA8GA1Ud
EwEB/wQFMAMBAf8wGgYDVR0RBBMwEYcEfwAAAYIJbG9jYWxob3N0MAoGCCqGSM49
BAMCA0cAMEQCIADqilTeRw5k7wzcGSsASInFaFh2YpymTD3ObmOYyNbmAiB8h98U
1Y+hXOUYzkgmHNP1EsPsvbuL65I53dTLanmA7w==
-----END CERTIFICATE-----`;
const testPrivateKey = `-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgiubW6nJ67aQtUMDz
a0KrDNfpa7a9ytCZbBgv+d0UhsmhRANCAASgCURbqD1cSbT61SPiNuSI0bYVwEvE
D57l1Tiz5klwicGGg7tnxdTxKHUu2wwOedAOgMVn5OzFYGkc4yT26yCu
-----END PRIVATE KEY-----`;
const config: RuntimeConfig = {
  primaryRoute: "web",
  listen: { host: "127.0.0.1", port: 0 },
  publicUrl: new URL("http://poisoned.test:55173"),
  boards: [{ id: "field", httpUrl: new URL("http://blackmagic.local"), tcpPort: 3456 }],
  services: { builder: "http://127.0.0.1:49101/" },
};

function waitForOpen(socket: WebSocket): Promise<void> {
  return new Promise((resolve, reject) => {
    socket.once("open", resolve);
    socket.once("error", reject);
  });
}

function waitForMessage(socket: WebSocket): Promise<Uint8Array> {
  return new Promise((resolve, reject) => {
    socket.once("message", (data, isBinary) => {
      if (!isBinary) reject(new Error("expected binary message"));
      else resolve(new Uint8Array(data as Buffer));
    });
    socket.once("error", reject);
  });
}

function waitForClose(socket: WebSocket): Promise<{ code: number; reason: string }> {
  return new Promise((resolve, reject) => {
    socket.once("close", (code, reason) => resolve({ code, reason: reason.toString("utf8") }));
    socket.once("error", reject);
  });
}

function getHttpsJson(address: string): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const request = httpsGet(`${address}/api/runtime/v1/manifest`, { rejectUnauthorized: false }, (response) => {
      const chunks: Buffer[] = [];
      response.on("data", (chunk: Buffer) => chunks.push(chunk));
      response.on("end", () => {
        try {
          resolve(JSON.parse(Buffer.concat(chunks).toString("utf8")));
        } catch (error) {
          reject(error);
        }
      });
    });
    request.on("error", reject);
  });
}

function within<T>(operation: Promise<T>, label: string): Promise<T> {
  return Promise.race([
    operation,
    new Promise<T>((_resolve, reject) => setTimeout(() => reject(new Error(`timed out: ${label}`)), 1_000)),
  ]);
}

describe("Poisoned_Os local Node runtime", () => {
  let runtime: RuntimeServer | undefined;
  let serviceServer: HttpServer | undefined;

  afterEach(async () => {
    await runtime?.close();
    runtime = undefined;
    if (serviceServer?.listening) {
      await new Promise<void>((resolve, reject) => serviceServer?.close((error) => error ? reject(error) : resolve()));
    }
    serviceServer = undefined;
  });

  it("publishes the web transport over mandatory Wi-Fi with independent process addresses", async () => {
    const controlPlane = new TestControlPlane();
    runtime = createRuntimeServer({
      config,
      controlPlane,
      registry: new ServiceAddressRegistry(),
      browserToken,
      adminToken,
    });
    const address = await runtime.listen();

    const response = await fetch(`${address}/api/runtime/v1/manifest`);
    expect(response.headers.get("cache-control")).toBe("no-store");
    expect(await response.json()).toEqual({
      version: 1,
      primaryRoute: "web",
      transport: {
        kind: "web",
        network: "wifi",
        discovery: { protocols: ["http", "https"], path: "/api/runtime/v1/manifest" },
        rpc: { protocols: ["ws", "wss"], pathTemplate: "/api/runtime/v1/boards/{boardId}/rpc" },
      },
      browserToken,
      boards: [{ id: "field", label: "Field Flipper", state: "available" }],
      services: {
        builder: {
          httpPath: "/api/runtime/v1/services/builder/proxy/",
          webSocketPath: "/api/runtime/v1/services/builder/proxy/",
        },
      },
    });
  });

  it("authenticates one browser socket and forwards only opaque RPC bytes", async () => {
    const controlPlane = new TestControlPlane();
    runtime = createRuntimeServer({
      config,
      controlPlane,
      registry: new ServiceAddressRegistry(),
      browserToken,
      adminToken,
    });
    const address = await runtime.listen();
    const protocol = `poisoned-os.web-rpc.v1.${browserToken}`;
    const socket = new WebSocket(
      address.replace("http:", "ws:") + "/api/runtime/v1/boards/field/rpc",
      protocol,
      { origin: address },
    );
    await waitForOpen(socket);
    expect(socket.protocol).toBe(protocol);

    socket.send(Uint8Array.of(0x08, 0x96, 0x01));
    await expect.poll(() => controlPlane.sessions[0].sent).toEqual([
      Uint8Array.of(0x08, 0x96, 0x01),
    ]);

    const response = waitForMessage(socket);
    controlPlane.sessions[0].receive(Uint8Array.of(0x10, 0x01));
    await expect(response).resolves.toEqual(Uint8Array.of(0x10, 0x01));
    socket.close();
  });

  it("closes WSS state when the Wi-Fi board drops so the browser can reconnect", async () => {
    const controlPlane = new TestControlPlane();
    runtime = createRuntimeServer({
      config,
      controlPlane,
      registry: new ServiceAddressRegistry(),
      browserToken,
      adminToken,
    });
    const address = await runtime.listen();
    const socket = new WebSocket(
      address.replace("http:", "ws:") + "/api/runtime/v1/boards/field/rpc",
      `poisoned-os.web-rpc.v1.${browserToken}`,
      { origin: address },
    );
    await waitForOpen(socket);
    const closed = waitForClose(socket);

    controlPlane.sessions[0].disconnect(new Error("board TCP connection lost"));

    await expect(within(closed, "board disconnect close")).resolves.toEqual({
      code: 1011,
      reason: "Wi-Fi board connection lost",
    });
    await new Promise((resolve) => setTimeout(resolve, 0));
    expect(controlPlane.inUse).toBe(false);

    const replacement = new WebSocket(
      address.replace("http:", "ws:") + "/api/runtime/v1/boards/field/rpc",
      `poisoned-os.web-rpc.v1.${browserToken}`,
      { origin: address },
    );
    await waitForOpen(replacement);
    replacement.close();
  });

  it("serves discovery over HTTPS and RPC over authenticated WSS", async () => {
    const controlPlane = new TestControlPlane();
    runtime = createRuntimeServer({
      config: { ...config, publicUrl: new URL("https://poisoned.test:55173") },
      controlPlane,
      registry: new ServiceAddressRegistry(),
      browserToken,
      adminToken,
      tls: { cert: testCertificate, key: testPrivateKey },
    });
    const address = await runtime.listen();
    expect(address.startsWith("https://")).toBe(true);
    const manifest = await getHttpsJson(address) as { primaryRoute: string; transport: { kind: string; network: string } };
    expect(manifest).toMatchObject({ primaryRoute: "web", transport: { kind: "web", network: "wifi" } });

    const socket = new WebSocket(
      address.replace("https:", "wss:") + "/api/runtime/v1/boards/field/rpc",
      `poisoned-os.web-rpc.v1.${browserToken}`,
      { origin: address, rejectUnauthorized: false },
    );
    await waitForOpen(socket);
    expect(socket.protocol).toBe(`poisoned-os.web-rpc.v1.${browserToken}`);
    socket.close();
  });

  it("rejects the wrong origin or token before opening the board", async () => {
    const controlPlane = new TestControlPlane();
    runtime = createRuntimeServer({
      config,
      controlPlane,
      registry: new ServiceAddressRegistry(),
      browserToken,
      adminToken,
    });
    const address = await runtime.listen();
    const badOrigin = new WebSocket(
      address.replace("http:", "ws:") + "/api/runtime/v1/boards/field/rpc",
      `poisoned-os.web-rpc.v1.${browserToken}`,
      { origin: "http://attacker.test" },
    );
    await expect(waitForOpen(badOrigin)).rejects.toThrow(/unexpected server response: 403/i);

    const badToken = new WebSocket(
      address.replace("http:", "ws:") + "/api/runtime/v1/boards/field/rpc",
      `poisoned-os.web-rpc.v1.${"c".repeat(64)}`,
      { origin: address },
    );
    await expect(waitForOpen(badToken)).rejects.toThrow(/unexpected server response: 401/i);
    expect(controlPlane.sessions).toEqual([]);
  });

  it("registers, renews, and releases separate authenticated Node process addresses", async () => {
    const registry = new ServiceAddressRegistry(() => 1_000);
    runtime = createRuntimeServer({
      config,
      controlPlane: new TestControlPlane(),
      registry,
      browserToken,
      adminToken,
    });
    const address = await runtime.listen();
    const headers = {
      "content-type": "application/json",
      "x-poison-runtime-admin": adminToken,
    };

    const register = await fetch(`${address}/api/runtime/v1/services`, {
      method: "POST",
      headers,
      body: JSON.stringify({ name: "workloads", url: "http://127.0.0.1:49102", ownerPid: 202, ttlMs: 5_000 }),
    });
    expect(register.status).toBe(201);

    const manifest = await fetch(`${address}/api/runtime/v1/manifest`);
    const manifestBody = await manifest.json() as { services: Record<string, unknown> };
    expect(manifestBody.services).toEqual({
      builder: {
        httpPath: "/api/runtime/v1/services/builder/proxy/",
        webSocketPath: "/api/runtime/v1/services/builder/proxy/",
      },
      workloads: {
        httpPath: "/api/runtime/v1/services/workloads/proxy/",
        webSocketPath: "/api/runtime/v1/services/workloads/proxy/",
      },
    });

    expect((await fetch(`${address}/api/runtime/v1/services/workloads`, {
      method: "PATCH",
      headers,
      body: JSON.stringify({ ownerPid: 202, ttlMs: 10_000 }),
    })).status).toBe(200);
    expect((await fetch(`${address}/api/runtime/v1/services/workloads`, {
      method: "DELETE",
      headers,
      body: JSON.stringify({ ownerPid: 202 }),
    })).status).toBe(204);
    expect(registry.snapshot()).toEqual([]);

    expect((await fetch(`${address}/api/runtime/v1/services`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ name: "evidence", url: "http://127.0.0.1:49103", ownerPid: 203, ttlMs: 5_000 }),
    })).status).toBe(401);
  });

  it("proxies named Node processes over authenticated same-origin HTTP and WebSocket routes", async () => {
    const serviceSockets = new WebSocketServer({ noServer: true });
    serviceServer = createHttpServer(async (request, response) => {
      const chunks: Buffer[] = [];
      for await (const chunk of request) chunks.push(Buffer.from(chunk));
      response.setHeader("content-type", "application/json");
      response.end(JSON.stringify({
        method: request.method,
        url: request.url,
        body: Buffer.concat(chunks).toString("utf8"),
      }));
    });
    serviceServer.on("upgrade", (request, socket, head) => {
      serviceSockets.handleUpgrade(request, socket, head, (webSocket) => {
        webSocket.on("message", (data, isBinary) => webSocket.send(data, { binary: isBinary }));
      });
    });
    await within(new Promise<void>((resolve) => serviceServer?.listen(0, "127.0.0.1", resolve)), "service listen");
    const serviceAddress = serviceServer.address();
    if (!serviceAddress || typeof serviceAddress === "string") throw new Error("test service did not bind TCP");

    const registry = new ServiceAddressRegistry();
    registry.register({
      name: "workloads",
      url: `http://127.0.0.1:${serviceAddress.port}`,
      ownerPid: 202,
      ttlMs: 60_000,
    });
    runtime = createRuntimeServer({
      config: { ...config, services: {} },
      controlPlane: new TestControlPlane(),
      registry,
      browserToken,
      adminToken,
    });
    const address = await within(runtime.listen(), "runtime listen");
    const manifestResponse = await within(fetch(`${address}/api/runtime/v1/manifest`), "manifest response");
    const manifestText = await within(manifestResponse.text(), "manifest body");
    expect(manifestText).not.toContain("127.0.0.1");
    const manifest = JSON.parse(manifestText) as {
      services: Record<string, { httpPath: string; webSocketPath: string }>;
    };

    const unauthorized = await within(fetch(`${address}${manifest.services.workloads.httpPath}health`), "unauthorized proxy");
    expect(unauthorized.status).toBe(401);
    const proxied = await within(fetch(`${address}${manifest.services.workloads.httpPath}jobs?state=active`, {
      method: "POST",
      headers: { "x-poison-runtime-browser": browserToken },
      body: "job-body",
    }), "HTTP proxy");
    await expect(proxied.json()).resolves.toEqual({
      method: "POST",
      url: "/jobs?state=active",
      body: "job-body",
    });

    const serviceSocket = new WebSocket(
      address.replace("http:", "ws:") + `${manifest.services.workloads.webSocketPath}events`,
      `poisoned-os.web-service.v1.${browserToken}`,
      { origin: address },
    );
    await within(waitForOpen(serviceSocket), "WebSocket proxy open");
    const echoed = waitForMessage(serviceSocket);
    serviceSocket.send(Uint8Array.of(0xde, 0xad));
    await expect(within(echoed, "WebSocket proxy echo")).resolves.toEqual(Uint8Array.of(0xde, 0xad));
    serviceSocket.terminate();
    for (const socket of serviceSockets.clients) socket.terminate();
    serviceSockets.close();
    serviceServer.closeAllConnections();
  });
});
