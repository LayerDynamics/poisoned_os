import assertSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/assert.js?raw";
import bufferSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/buffer.js?raw";
import cryptoSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/crypto.js?raw";
import espTransportSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/esp_transport.js?raw";
import eventsSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/events.js?raw";
import fsSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/fs.js?raw";
import httpSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/http.js?raw";
import netSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/net.js?raw";
import osSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/os.js?raw";
import pathSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/path.js?raw";
import processSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/process.js?raw";
import promiseSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/promise.js?raw";
import querystringSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/querystring.js?raw";
import streamSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/stream.js?raw";
import stringDecoderSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/string_decoder.js?raw";
import timersSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/timers.js?raw";
import tlsSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/tls.js?raw";
import urlSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/url.js?raw";
import utilSource from "../../../../applications/system/js_app/packages/fz-sdk/shims/util.js?raw";

const RESERVED_ROOT = "_poison/node/";
const REQUIRE = /\brequire\s*\(\s*["']([^"']+)["']\s*\)/g;

const modules = {
  assert: { file: "assert.js", source: assertSource, dependencies: [] },
  buffer: { file: "buffer.js", source: bufferSource, dependencies: [] },
  crypto: { file: "crypto.js", source: cryptoSource, dependencies: [] },
  events: { file: "events.js", source: eventsSource, dependencies: [] },
  fs: { file: "fs.js", source: fsSource, dependencies: [] },
  http: { file: "http.js", source: httpSource, dependencies: ["events", "esp_transport"] },
  https: { file: "http.js", source: httpSource, dependencies: ["events", "esp_transport"] },
  net: { file: "net.js", source: netSource, dependencies: ["events", "esp_transport"] },
  os: { file: "os.js", source: osSource, dependencies: [] },
  path: { file: "path.js", source: pathSource, dependencies: [] },
  process: { file: "process.js", source: processSource, dependencies: ["os"] },
  promise: { file: "promise.js", source: promiseSource, dependencies: ["process"] },
  querystring: { file: "querystring.js", source: querystringSource, dependencies: [] },
  stream: { file: "stream.js", source: streamSource, dependencies: ["events"] },
  string_decoder: { file: "string_decoder.js", source: stringDecoderSource, dependencies: [] },
  timers: { file: "timers.js", source: timersSource, dependencies: [] },
  tls: { file: "tls.js", source: tlsSource, dependencies: ["net"] },
  url: { file: "url.js", source: urlSource, dependencies: [] },
  util: { file: "util.js", source: utilSource, dependencies: [] },
  esp_transport: { file: "esp_transport.js", source: espTransportSource, dependencies: [] },
} as const;

type RuntimeModule = keyof typeof modules;

function runtimeModule(specifier: string): RuntimeModule | null {
  const name = specifier.startsWith("node:") ? specifier.slice(5) : specifier;
  return Object.hasOwn(modules, name) && name !== "esp_transport" ? name as RuntimeModule : null;
}

function requestedRuntimeModules(
  projectFiles: Readonly<Record<string, string>>,
): ReadonlySet<RuntimeModule> {
  if(Object.keys(projectFiles).some((path) => path.startsWith(RESERVED_ROOT))) {
    throw new Error(`${RESERVED_ROOT} is reserved for runtime built-ins`);
  }
  const requested = new Set<RuntimeModule>();
  for(const [path, source] of Object.entries(projectFiles)) {
    if(!/\.(?:js|mjs|cjs)$/.test(path)) continue;
    REQUIRE.lastIndex = 0;
    for(let match = REQUIRE.exec(source); match; match = REQUIRE.exec(source)) {
      const module = runtimeModule(match[1]);
      if(module) requested.add(module);
    }
  }
  const include = (name: RuntimeModule): void => {
    if(requested.has(name) && name === "esp_transport") return;
    requested.add(name);
    for(const dependency of modules[name].dependencies) include(dependency);
  };
  for(const name of [...requested]) include(name);
  return requested;
}

export function runtimeBuiltinCapabilities(
  projectFiles: Readonly<Record<string, string>>,
): ReadonlySet<string> {
  const requested = requestedRuntimeModules(projectFiles);
  const capabilities = new Set<string>();
  if(requested.has("os")) capabilities.add("device");
  if(requested.has("process") || requested.has("timers")) capabilities.add("runtime");
  if(requested.has("fs")) capabilities.add("storage");
  if(requested.has("crypto")) capabilities.add("crypto");
  if(requested.has("esp_transport")) capabilities.add("serial");
  return capabilities;
}

export function runtimeBuiltinFiles(
  projectFiles: Readonly<Record<string, string>>,
): Readonly<Record<string, string>> {
  const requested = requestedRuntimeModules(projectFiles);
  const result: Record<string, string> = {};
  for(const name of [...requested].sort()) {
    const module = modules[name];
    result[`${RESERVED_ROOT}${module.file}`] = module.source;
  }
  return result;
}
