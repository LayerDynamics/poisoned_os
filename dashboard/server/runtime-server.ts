import { timingSafeEqual, randomUUID } from "node:crypto";
import {
  createServer,
  request as requestHttp,
  type IncomingHttpHeaders,
  type IncomingMessage,
  type RequestListener,
  type ServerResponse,
} from "node:http";
import { createServer as createSecureServer, request as requestHttps } from "node:https";
import type { AddressInfo } from "node:net";
import type { Duplex } from "node:stream";
import type { SecureContextOptions } from "node:tls";
import WebSocket, { WebSocketServer, type RawData } from "ws";
import type { RuntimeConfig } from "./runtime-config";
import { ServiceAddressRegistry, type ServiceAddressLease } from "./service-address-registry";
import type { WifiBoardStatus, WifiRpcSession } from "./wifi-control-plane";

interface RuntimeControlPlane {
  statuses(): WifiBoardStatus[];
  openSession(boardId: string, clientId: string): Promise<WifiRpcSession>;
}

export interface RuntimeServerOptions {
  config: RuntimeConfig;
  controlPlane: RuntimeControlPlane;
  registry: ServiceAddressRegistry;
  browserToken: string;
  adminToken: string;
  tls?: SecureContextOptions;
  fallback?: RequestListener;
}

export interface RuntimeServer {
  listen(): Promise<string>;
  close(): Promise<void>;
}

const TOKEN_PATTERN = /^[0-9a-f]{64}$/;
const MAX_BODY_BYTES = 16 * 1024;
const HOP_BY_HOP_HEADERS = new Set([
  "connection",
  "keep-alive",
  "proxy-authenticate",
  "proxy-authorization",
  "te",
  "trailer",
  "transfer-encoding",
  "upgrade",
]);

function protocolFor(token: string): string {
  return `poisoned-os.web-rpc.v1.${token}`;
}

function serviceProtocolFor(token: string): string {
  return `poisoned-os.web-service.v1.${token}`;
}

function safeEqual(left: string, right: string): boolean {
  const leftBytes = Buffer.from(left);
  const rightBytes = Buffer.from(right);
  return leftBytes.byteLength === rightBytes.byteLength && timingSafeEqual(leftBytes, rightBytes);
}

function setSecurityHeaders(response: ServerResponse): void {
  response.setHeader("x-content-type-options", "nosniff");
  response.setHeader("referrer-policy", "no-referrer");
  response.setHeader("cache-control", "no-store");
}

function sendJson(response: ServerResponse, status: number, value: unknown): void {
  setSecurityHeaders(response);
  response.statusCode = status;
  response.setHeader("content-type", "application/json; charset=utf-8");
  response.end(JSON.stringify(value));
}

function sendError(response: ServerResponse, status: number, message: string): void {
  sendJson(response, status, { error: message });
}

async function readJson(request: IncomingMessage): Promise<Record<string, unknown>> {
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of request) {
    const bytes = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
    size += bytes.byteLength;
    if (size > MAX_BODY_BYTES) throw new Error("request body exceeds 16 KiB");
    chunks.push(bytes);
  }

  let value: unknown;
  try {
    value = JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    throw new Error("request body must be valid JSON");
  }
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new Error("request body must be a JSON object");
  }
  return value as Record<string, unknown>;
}

function requireNumber(body: Record<string, unknown>, key: string): number {
  const value = body[key];
  if (typeof value !== "number") throw new Error(`${key} must be a number`);
  return value;
}

function requireString(body: Record<string, unknown>, key: string): string {
  const value = body[key];
  if (typeof value !== "string") throw new Error(`${key} must be a string`);
  return value;
}

function rawDataBytes(data: RawData): Uint8Array {
  if (Array.isArray(data)) return new Uint8Array(Buffer.concat(data));
  if (data instanceof ArrayBuffer) return new Uint8Array(data);
  return new Uint8Array(data.buffer, data.byteOffset, data.byteLength).slice();
}

function rejectUpgrade(socket: Duplex, status: number, message: string): void {
  const reason = status === 401 ? "Unauthorized" :
    status === 403 ? "Forbidden" :
      status === 404 ? "Not Found" :
        status === 502 ? "Bad Gateway" :
          status === 504 ? "Gateway Timeout" : "Conflict";
  const body = JSON.stringify({ error: message });
  socket.end(
    `HTTP/1.1 ${status} ${reason}\r\n` +
    "Connection: close\r\n" +
    "Content-Type: application/json\r\n" +
    `Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`,
  );
}

function requestOrigin(request: IncomingMessage): string | undefined {
  const host = request.headers.host;
  if (!host) return undefined;
  const protocol = (request.socket as typeof request.socket & { encrypted?: boolean }).encrypted ? "https" : "http";
  return `${protocol}://${host}`;
}

function proxyHeaders(headers: IncomingHttpHeaders): IncomingHttpHeaders {
  const filtered: IncomingHttpHeaders = {};
  for (const [name, value] of Object.entries(headers)) {
    if (value === undefined || HOP_BY_HOP_HEADERS.has(name) ||
        name === "host" || name === "x-poison-runtime-admin" || name === "x-poison-runtime-browser") continue;
    filtered[name] = value;
  }
  return filtered;
}

export function createRuntimeServer(options: RuntimeServerOptions): RuntimeServer {
  if (!TOKEN_PATTERN.test(options.browserToken)) throw new Error("browser token must be 64 lowercase hexadecimal characters");
  if (!TOKEN_PATTERN.test(options.adminToken)) throw new Error("admin token must be 64 lowercase hexadecimal characters");
  if (safeEqual(options.browserToken, options.adminToken)) throw new Error("browser and admin tokens must be different");

  const expectedProtocol = protocolFor(options.browserToken);
  const expectedServiceProtocol = serviceProtocolFor(options.browserToken);
  const activeSessions = new Set<WifiRpcSession>();
  const serviceBrowsers = new Set<WebSocket>();
  const servicePeers = new Set<WebSocket>();
  const webSockets = new WebSocketServer({
    noServer: true,
    maxPayload: 1024 * 1024,
    handleProtocols: (protocols) => protocols.has(expectedProtocol) ? expectedProtocol : false,
  });
  const serviceWebSockets = new WebSocketServer({
    noServer: true,
    maxPayload: 16 * 1024 * 1024,
    handleProtocols: (protocols) => protocols.has(expectedServiceProtocol) ? expectedServiceProtocol : false,
  });

  const serviceAddresses = (): Record<string, string> => {
    const services = { ...options.config.services };
    for (const lease of options.registry.snapshot()) services[lease.name] = lease.url;
    return services;
  };

  const publishedServices = (): Record<string, { httpPath: string; webSocketPath: string }> =>
    Object.fromEntries(Object.keys(serviceAddresses()).sort().map((name) => {
      const path = `/api/runtime/v1/services/${encodeURIComponent(name)}/proxy/`;
      return [name, { httpPath: path, webSocketPath: path }];
    }));

  const isAdmin = (request: IncomingMessage): boolean => {
    const header = request.headers["x-poison-runtime-admin"];
    return typeof header === "string" && safeEqual(header, options.adminToken);
  };

  const isBrowser = (request: IncomingMessage): boolean => {
    const header = request.headers["x-poison-runtime-browser"];
    return typeof header === "string" && safeEqual(header, options.browserToken);
  };

  const serviceProxyTarget = (requestUrl: URL): { name: string; target: URL } | undefined => {
    const match = /^\/api\/runtime\/v1\/services\/([^/]+)\/proxy\/(.*)$/.exec(requestUrl.pathname);
    if (!match) return undefined;
    let name: string;
    try {
      name = decodeURIComponent(match[1]);
    } catch {
      return undefined;
    }
    const address = serviceAddresses()[name];
    if (!address) return undefined;
    const base = new URL(address);
    if (!base.pathname.endsWith("/")) base.pathname += "/";
    const target = new URL(match[2], base);
    if (target.origin !== base.origin || !target.pathname.startsWith(base.pathname)) return undefined;
    target.search = requestUrl.search;
    return { name, target };
  };

  const proxyServiceHttp = (
    request: IncomingMessage,
    response: ServerResponse,
    target: URL,
  ): void => {
    const requestUpstream = target.protocol === "https:" ? requestHttps : requestHttp;
    const upstream = requestUpstream(target, {
      method: request.method,
      headers: { ...proxyHeaders(request.headers), host: target.host },
      timeout: 30_000,
    }, (upstreamResponse) => {
      response.statusCode = upstreamResponse.statusCode ?? 502;
      for (const [name, value] of Object.entries(proxyHeaders(upstreamResponse.headers))) {
        if (value !== undefined) response.setHeader(name, value);
      }
      response.setHeader("x-content-type-options", "nosniff");
      upstreamResponse.pipe(response);
    });
    upstream.on("timeout", () => upstream.destroy(new Error("Node service proxy timed out")));
    upstream.on("error", (error) => {
      if (!response.headersSent) sendError(response, 502, `Node service proxy failed: ${error.message}`);
      else response.destroy(error);
    });
    request.on("aborted", () => upstream.destroy());
    request.pipe(upstream);
  };

  const serviceNameFromPath = (pathname: string): string | undefined => {
    const match = /^\/api\/runtime\/v1\/services\/([^/]+)$/.exec(pathname);
    if (!match) return undefined;
    try {
      return decodeURIComponent(match[1]);
    } catch {
      return undefined;
    }
  };

  const handleServiceRequest = async (
    request: IncomingMessage,
    response: ServerResponse,
    pathname: string,
  ): Promise<boolean> => {
    const collection = pathname === "/api/runtime/v1/services";
    const serviceName = serviceNameFromPath(pathname);
    if (!collection && serviceName === undefined) return false;
    if (!isAdmin(request)) {
      sendError(response, 401, "Node service registry authentication failed");
      return true;
    }

    try {
      if (collection && request.method === "POST") {
        const body = await readJson(request);
        const registration = {
          name: requireString(body, "name"),
          url: requireString(body, "url"),
          ownerPid: requireNumber(body, "ownerPid"),
          ttlMs: requireNumber(body, "ttlMs"),
        };
        if (options.config.services[registration.name]) {
          throw new Error(`Node service name ${registration.name} is statically configured`);
        }
        if (Object.values(options.config.services).includes(new URL(registration.url).href)) {
          throw new Error(`Node service address ${registration.url} is statically configured`);
        }
        sendJson(response, 201, options.registry.register(registration));
        return true;
      }

      if (serviceName !== undefined && request.method === "PATCH") {
        const body = await readJson(request);
        sendJson(response, 200, options.registry.renew(
          serviceName,
          requireNumber(body, "ownerPid"),
          requireNumber(body, "ttlMs"),
        ));
        return true;
      }

      if (serviceName !== undefined && request.method === "DELETE") {
        const body = await readJson(request);
        options.registry.release(serviceName, requireNumber(body, "ownerPid"));
        setSecurityHeaders(response);
        response.statusCode = 204;
        response.end();
        return true;
      }

      sendError(response, 405, "method not allowed");
      return true;
    } catch (error) {
      sendError(response, 400, error instanceof Error ? error.message : String(error));
      return true;
    }
  };

  const requestHandler: RequestListener = async (request, response) => {
    try {
      const url = new URL(request.url ?? "/", options.config.publicUrl);
      if (request.method === "GET" && url.pathname === "/api/runtime/v1/manifest") {
        sendJson(response, 200, {
          version: 1,
          primaryRoute: "web",
          transport: {
            kind: "web",
            network: "wifi",
            discovery: {
              protocols: ["http", "https"],
              path: "/api/runtime/v1/manifest",
            },
            rpc: {
              protocols: ["ws", "wss"],
              pathTemplate: "/api/runtime/v1/boards/{boardId}/rpc",
            },
          },
          browserToken: options.browserToken,
          boards: options.controlPlane.statuses(),
          services: publishedServices(),
        });
        return;
      }

      const serviceProxy = serviceProxyTarget(url);
      if (serviceProxy) {
        if (!isBrowser(request)) {
          sendError(response, 401, "Node service proxy authentication failed");
          return;
        }
        proxyServiceHttp(request, response, serviceProxy.target);
        return;
      }

      if (await handleServiceRequest(request, response, url.pathname)) return;
      if (options.fallback) {
        options.fallback(request, response);
      } else {
        sendError(response, 404, "not found");
      }
    } catch (error) {
      sendError(response, 500, error instanceof Error ? error.message : String(error));
    }
  };
  const httpServer = options.tls
    ? createSecureServer(options.tls, requestHandler)
    : createServer(requestHandler);

  const bindSession = (socket: WebSocket, session: WifiRpcSession): void => {
    activeSessions.add(session);
    const removeData = session.onData((data) => {
      if (socket.readyState === WebSocket.OPEN) socket.send(data, { binary: true });
    });
    const removeDisconnect = session.onDisconnect(() => {
      removeData();
      activeSessions.delete(session);
      if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING) {
        socket.close(1011, "Wi-Fi board connection lost");
      }
    });
    let sendTail = Promise.resolve();
    let closed = false;

    const closeSession = async () => {
      if (closed) return;
      closed = true;
      removeData();
      removeDisconnect();
      activeSessions.delete(session);
      await session.close();
    };

    socket.on("message", (data, isBinary) => {
      if (!isBinary) {
        socket.close(1003, "binary RPC frames required");
        return;
      }
      const bytes = rawDataBytes(data);
      sendTail = sendTail.then(() => session.send(bytes));
      void sendTail.catch(() => socket.close(1011, "board RPC write failed"));
    });
    socket.on("close", () => void closeSession());
    socket.on("error", () => void closeSession());
  };

  const bindServiceProxy = (browser: WebSocket, upstream: WebSocket): void => {
    serviceBrowsers.add(browser);
    servicePeers.add(upstream);
    const closePeer = (peer: WebSocket, code = 1001, reason = "service proxy closed") => {
      if (peer.readyState === WebSocket.OPEN || peer.readyState === WebSocket.CONNECTING) peer.close(code, reason);
    };
    browser.on("message", (data, isBinary) => {
      if (upstream.readyState === WebSocket.OPEN) upstream.send(data, { binary: isBinary });
    });
    upstream.on("message", (data, isBinary) => {
      if (browser.readyState === WebSocket.OPEN) browser.send(data, { binary: isBinary });
    });
    browser.on("close", () => {
      serviceBrowsers.delete(browser);
      closePeer(upstream);
    });
    upstream.on("close", () => {
      servicePeers.delete(upstream);
      closePeer(browser);
    });
    browser.on("error", () => closePeer(upstream, 1011, "browser service socket failed"));
    upstream.on("error", () => {
      servicePeers.delete(upstream);
      closePeer(browser, 1011, "Node service socket failed");
    });
  };

  httpServer.on("upgrade", (request, socket, head) => {
    const url = new URL(request.url ?? "/", options.config.publicUrl);
    const serviceProxy = serviceProxyTarget(url);
    if (serviceProxy) {
      if (request.headers.origin !== requestOrigin(request)) {
        rejectUpgrade(socket, 403, "Node service WebSocket origin is not allowed");
        return;
      }
      const offeredProtocols = String(request.headers["sec-websocket-protocol"] ?? "")
        .split(",")
        .map((value) => value.trim());
      if (!offeredProtocols.some((protocol) => safeEqual(protocol, expectedServiceProtocol))) {
        rejectUpgrade(socket, 401, "Node service WebSocket authentication failed");
        return;
      }
      const upstreamUrl = new URL(serviceProxy.target);
      upstreamUrl.protocol = upstreamUrl.protocol === "https:" ? "wss:" : "ws:";
      const upstream = new WebSocket(upstreamUrl, { handshakeTimeout: 5_000 });
      const timeout = setTimeout(() => {
        upstream.terminate();
        if (!socket.destroyed) rejectUpgrade(socket, 504, "Node service WebSocket connection timed out");
      }, 5_000);
      timeout.unref();
      const upstreamError = (error: Error) => {
        clearTimeout(timeout);
        if (!socket.destroyed) rejectUpgrade(socket, 502, `Node service WebSocket connection failed: ${error.message}`);
      };
      upstream.once("error", upstreamError);
      upstream.once("open", () => {
        clearTimeout(timeout);
        upstream.off("error", upstreamError);
        if (socket.destroyed) {
          upstream.terminate();
          return;
        }
        serviceWebSockets.handleUpgrade(request, socket, head, (browser) => {
          bindServiceProxy(browser, upstream);
        });
      });
      return;
    }
    const match = /^\/api\/runtime\/v1\/boards\/([^/]+)\/rpc$/.exec(url.pathname);
    if (!match) {
      rejectUpgrade(socket, 404, "Wi-Fi RPC endpoint not found");
      return;
    }
    if (request.headers.origin !== requestOrigin(request)) {
      rejectUpgrade(socket, 403, "Wi-Fi RPC origin is not allowed");
      return;
    }
    const offeredProtocols = String(request.headers["sec-websocket-protocol"] ?? "")
      .split(",")
      .map((value) => value.trim());
    if (!offeredProtocols.some((protocol) => safeEqual(protocol, expectedProtocol))) {
      rejectUpgrade(socket, 401, "Wi-Fi RPC authentication failed");
      return;
    }

    let boardId: string;
    try {
      boardId = decodeURIComponent(match[1]);
    } catch {
      rejectUpgrade(socket, 404, "Wi-Fi board id is invalid");
      return;
    }

    void options.controlPlane.openSession(boardId, randomUUID()).then((session) => {
      if (socket.destroyed) {
        void session.close();
        return;
      }
      webSockets.handleUpgrade(request, socket, head, (webSocket) => {
        webSockets.emit("connection", webSocket, request);
        bindSession(webSocket, session);
      });
    }).catch((error: unknown) => {
      rejectUpgrade(socket, 409, error instanceof Error ? error.message : String(error));
    });
  });

  let listening = false;
  return {
    listen: () => new Promise((resolve, reject) => {
      if (listening) {
        reject(new Error("Node runtime is already listening"));
        return;
      }
      const onError = (error: Error) => {
        httpServer.off("listening", onListening);
        reject(error);
      };
      const onListening = () => {
        httpServer.off("error", onError);
        listening = true;
        const address = httpServer.address() as AddressInfo;
        const host = options.config.listen.host === "0.0.0.0" ? "127.0.0.1" : options.config.listen.host;
        const formattedHost = host.includes(":") ? `[${host}]` : host;
        resolve(`${options.tls ? "https" : "http"}://${formattedHost}:${address.port}`);
      };
      httpServer.once("error", onError);
      httpServer.once("listening", onListening);
      httpServer.listen(options.config.listen.port, options.config.listen.host);
    }),
    close: async () => {
      for (const client of webSockets.clients) client.terminate();
      for (const client of serviceWebSockets.clients) client.terminate();
      for (const browser of serviceBrowsers) browser.terminate();
      for (const peer of servicePeers) peer.terminate();
      serviceBrowsers.clear();
      servicePeers.clear();
      await Promise.allSettled([...activeSessions].map((session) => session.close()));
      activeSessions.clear();
      if (!listening) return;
      await new Promise<void>((resolve, reject) => {
        httpServer.close((error) => error ? reject(error) : resolve());
      });
      listening = false;
    },
  };
}

export type { ServiceAddressLease };
