import { randomBytes } from "node:crypto";
import { createReadStream } from "node:fs";
import { readFile, stat } from "node:fs/promises";
import type { IncomingMessage, RequestListener, ServerResponse } from "node:http";
import { dirname, extname, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { createServer as createViteServer, type ViteDevServer } from "vite";
import { loadRuntimeConfig } from "./runtime-config";
import { createRuntimeServer } from "./runtime-server";
import { ServiceAddressRegistry } from "./service-address-registry";
import { WifiControlPlane } from "./wifi-control-plane";

const MIME_TYPES: Record<string, string> = {
  ".css": "text/css; charset=utf-8",
  ".html": "text/html; charset=utf-8",
  ".ico": "image/x-icon",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".map": "application/json; charset=utf-8",
  ".png": "image/png",
  ".svg": "image/svg+xml",
  ".woff": "font/woff",
  ".woff2": "font/woff2",
};

interface DashboardFallback {
  handler: RequestListener;
  close(): Promise<void>;
}

function sendNotFound(response: ServerResponse): void {
  response.statusCode = 404;
  response.setHeader("content-type", "text/plain; charset=utf-8");
  response.end("Not found");
}

async function createDevelopmentFallback(): Promise<DashboardFallback> {
  const vite: ViteDevServer = await createViteServer({
    appType: "spa",
    server: { middlewareMode: true },
  });
  return {
    handler: (request, response) => {
      vite.middlewares(request, response, () => sendNotFound(response));
    },
    close: () => vite.close(),
  };
}

function applyDashboardHeaders(response: ServerResponse, contentType: string): void {
  response.setHeader("content-type", contentType);
  response.setHeader("x-content-type-options", "nosniff");
  response.setHeader("referrer-policy", "no-referrer");
  response.setHeader(
    "content-security-policy",
    "default-src 'self'; connect-src 'self' ws: wss:; img-src 'self' data:; style-src 'self'; font-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'",
  );
}

async function resolveDashboardFile(distributionRoot: string, request: IncomingMessage): Promise<string> {
  const pathname = decodeURIComponent(new URL(request.url ?? "/", "http://runtime.local").pathname);
  const relativePath = pathname === "/" ? "index.html" : pathname.replace(/^\/+/, "");
  const candidate = resolve(distributionRoot, relativePath);
  if (candidate !== distributionRoot && !candidate.startsWith(`${distributionRoot}${sep}`)) {
    throw new Error("Dashboard path escapes the distribution directory");
  }

  try {
    if ((await stat(candidate)).isFile()) return candidate;
  } catch {
    // The dashboard is a single-page application; unknown routes load its entry point.
  }
  return resolve(distributionRoot, "index.html");
}

async function createProductionFallback(): Promise<DashboardFallback> {
  const serverDirectory = dirname(fileURLToPath(import.meta.url));
  const distributionRoot = resolve(serverDirectory, "../dist");
  const entrypoint = resolve(distributionRoot, "index.html");
  try {
    if (!(await stat(entrypoint)).isFile()) throw new Error("not a file");
  } catch {
    throw new Error("Dashboard build is missing; run `pnpm build` before `pnpm start`");
  }

  return {
    handler: (request, response) => {
      void resolveDashboardFile(distributionRoot, request).then((filePath) => {
        applyDashboardHeaders(response, MIME_TYPES[extname(filePath)] ?? "application/octet-stream");
        if (request.method === "HEAD") {
          response.end();
          return;
        }
        const stream = createReadStream(filePath);
        stream.on("error", () => sendNotFound(response));
        stream.pipe(response);
      }).catch(() => sendNotFound(response));
    },
    close: () => Promise.resolve(),
  };
}

function configuredToken(value: string | undefined): string {
  return value && value.length > 0 ? value : randomBytes(32).toString("hex");
}

async function main(): Promise<void> {
  const development = process.argv.includes("--dev");
  const config = loadRuntimeConfig();
  const browserToken = configuredToken(process.env.POISON_RUNTIME_BROWSER_TOKEN);
  const adminToken = configuredToken(process.env.POISON_RUNTIME_ADMIN_TOKEN);
  const dashboard = development
    ? await createDevelopmentFallback()
    : await createProductionFallback();
  const controlPlane = new WifiControlPlane(config.boards);
  const tls = config.tls ? {
    cert: await readFile(config.tls.certPath),
    key: await readFile(config.tls.keyPath),
  } : undefined;
  const runtime = createRuntimeServer({
    config,
    controlPlane,
    registry: new ServiceAddressRegistry(),
    browserToken,
    adminToken,
    tls,
    fallback: dashboard.handler,
  });

  const listenUrl = await runtime.listen();
  process.stdout.write(`Poisoned_Os Node runtime listening on ${listenUrl}\n`);
  if (config.publicUrl.origin !== listenUrl) {
    process.stdout.write(`Browser address: ${config.publicUrl.origin}\n`);
  }
  process.stdout.write(`Wi-Fi boards: ${config.boards.map((board) => `${board.id}=${board.httpUrl.origin}`).join(", ")}\n`);
  process.stdout.write(`Node service admin token: ${adminToken}\n`);

  let closing = false;
  const close = async () => {
    if (closing) return;
    closing = true;
    await runtime.close();
    await dashboard.close();
  };
  process.once("SIGINT", () => void close().then(() => process.exit(0)));
  process.once("SIGTERM", () => void close().then(() => process.exit(0)));
}

void main().catch((error: unknown) => {
  process.stderr.write(`Poisoned_Os runtime failed: ${error instanceof Error ? error.message : String(error)}\n`);
  process.exitCode = 1;
});
