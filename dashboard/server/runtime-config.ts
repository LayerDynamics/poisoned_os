import { normalizeLocalServiceUrl } from "./service-address-registry";

export interface RuntimeBoardConfig {
  id: string;
  httpUrl: URL;
  tcpPort: number;
}

export interface RuntimeConfig {
  primaryRoute: "web";
  listen: { host: string; port: number };
  publicUrl: URL;
  tls?: { certPath: string; keyPath: string };
  boards: RuntimeBoardConfig[];
  services: Record<string, string>;
}

const NAME_PATTERN = /^[a-z][a-z0-9-]{0,62}$/;
const DEFAULT_LISTEN = "127.0.0.1:55173";
const DEFAULT_PUBLIC_URL = "http://127.0.0.1:55173";
const DEFAULT_BOARDS = { primary: "http://blackmagic.local" };

function parseListen(value: string): { host: string; port: number } {
  const bracketed = /^\[([^\]]+)]:(\d+)$/.exec(value);
  const plain = /^([^:]+):(\d+)$/.exec(value);
  const match = bracketed ?? plain;
  if (!match) {
    throw new Error("POISON_RUNTIME_LISTEN must be a host:port address");
  }

  const port = Number(match[2]);
  if (!Number.isInteger(port) || port < 1 || port > 65_535) {
    throw new Error("POISON_RUNTIME_LISTEN port must be between 1 and 65535");
  }
  return { host: match[1], port };
}

function isLoopbackHost(host: string): boolean {
  const normalized = host.toLowerCase();
  return normalized === "localhost" || normalized === "::1" || /^127(?:\.\d{1,3}){3}$/.test(normalized);
}

function parseHttpUrl(value: string, label: string): URL {
  let url: URL;
  try {
    url = new URL(value);
  } catch {
    throw new Error(`${label} is not a valid URL: ${value}`);
  }
  if (url.protocol !== "http:" && url.protocol !== "https:") {
    throw new Error(`${label} must use HTTP or HTTPS`);
  }
  if (url.username || url.password || url.search || url.hash) {
    throw new Error(`${label} must not contain credentials, a query, or a fragment`);
  }
  return url;
}

function parseObject(value: string | undefined, fallback: Record<string, string>, label: string): Record<string, string> {
  if (value === undefined || value.trim() === "") {
    return fallback;
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(value);
  } catch {
    throw new Error(`${label} must be a JSON object of names to URLs`);
  }
  if (parsed === null || Array.isArray(parsed) || typeof parsed !== "object") {
    throw new Error(`${label} must be a JSON object of names to URLs`);
  }

  const entries = Object.entries(parsed);
  if (entries.some(([, url]) => typeof url !== "string")) {
    throw new Error(`${label} values must be URLs`);
  }
  return Object.fromEntries(entries) as Record<string, string>;
}

function validateName(name: string, label: string): void {
  if (!NAME_PATTERN.test(name)) {
    throw new Error(`${label} contains an invalid name: ${name}`);
  }
}

function parseBoards(value: string | undefined): RuntimeBoardConfig[] {
  const configured = parseObject(value, DEFAULT_BOARDS, "POISON_WIFI_BOARDS");
  const boards: RuntimeBoardConfig[] = [];
  const addresses = new Set<string>();

  for (const [id, address] of Object.entries(configured)) {
    validateName(id, "POISON_WIFI_BOARDS");
    const httpUrl = parseHttpUrl(address, `Wi-Fi board ${id}`);
    if (httpUrl.pathname !== "/") {
      throw new Error(`Wi-Fi board ${id} URL must identify the board origin, not an API path`);
    }
    const key = httpUrl.origin.toLowerCase();
    if (addresses.has(key)) {
      throw new Error(`Duplicate Wi-Fi board address: ${httpUrl.origin}`);
    }
    addresses.add(key);
    boards.push({ id, httpUrl, tcpPort: 3456 });
  }

  if (boards.length === 0) {
    throw new Error("POISON_WIFI_BOARDS must configure at least one Wi-Fi board");
  }
  return boards;
}

function parseServices(value: string | undefined): Record<string, string> {
  const configured = parseObject(value, {}, "POISON_NODE_SERVICES");
  const services: Record<string, string> = {};
  const addresses = new Set<string>();

  for (const [name, address] of Object.entries(configured)) {
    validateName(name, "POISON_NODE_SERVICES");
    const normalized = normalizeLocalServiceUrl(address);
    if (addresses.has(normalized)) {
      throw new Error(`Duplicate local Node service address: ${normalized}`);
    }
    addresses.add(normalized);
    services[name] = normalized;
  }
  return services;
}

export function loadRuntimeConfig(environment: Record<string, string | undefined> = process.env): RuntimeConfig {
  const listen = parseListen(environment.POISON_RUNTIME_LISTEN ?? DEFAULT_LISTEN);
  const publicUrl = parseHttpUrl(
    environment.POISON_RUNTIME_PUBLIC_URL ?? DEFAULT_PUBLIC_URL,
    "POISON_RUNTIME_PUBLIC_URL",
  );
  const certPath = environment.POISON_RUNTIME_TLS_CERT?.trim();
  const keyPath = environment.POISON_RUNTIME_TLS_KEY?.trim();
  if (Boolean(certPath) !== Boolean(keyPath)) {
    throw new Error("POISON_RUNTIME_TLS_CERT and POISON_RUNTIME_TLS_KEY must be configured together");
  }
  const tls = certPath && keyPath ? { certPath, keyPath } : undefined;
  if (publicUrl.protocol === "https:" && !tls) {
    throw new Error("HTTPS requires POISON_RUNTIME_TLS_CERT and POISON_RUNTIME_TLS_KEY");
  }
  if (publicUrl.protocol === "http:" && tls) {
    throw new Error("TLS certificate and key require an HTTPS POISON_RUNTIME_PUBLIC_URL");
  }
  if (!isLoopbackHost(listen.host) && !tls) {
    throw new Error("A non-loopback dashboard listener requires HTTPS and a TLS certificate/key");
  }

  return {
    primaryRoute: "web",
    listen,
    publicUrl,
    tls,
    boards: parseBoards(environment.POISON_WIFI_BOARDS),
    services: parseServices(environment.POISON_NODE_SERVICES),
  };
}
