import { useEffect, useMemo, useState, type ChangeEvent, type ReactElement } from "react";
import { sha256Hex } from "../../files/FileTransferQueue";

const SCHEMA = "poison.javascript.lock/v1";
const RUNTIME = "poison-mjs-1";
const MAX_DEPENDENCIES = 256;
const MAX_FILES = 256;
const MAX_FILE_BYTES = 256 * 1024;
const MAX_TOTAL_BYTES = 4 * 1024 * 1024;
const MAX_GRAPH_DEPTH = 16;
const NAME = /^(?:@[a-z0-9][a-z0-9._-]*\/)?[a-z0-9][a-z0-9._-]*$/;
const VERSION = /^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$/;
const LICENSE = /^[A-Za-z0-9][A-Za-z0-9.()+\- ]{0,127}$/;
const DIGEST = /^[0-9a-f]{64}$/;
const SOURCE_PATH = /^(?:[A-Za-z0-9@_+-][A-Za-z0-9@._+-]*\/)*[A-Za-z0-9@_+-][A-Za-z0-9@._+-]*\.(?:js|mjs|cjs|ts|json)$/;
const LOCK_KEYS = ["dependencies", "entrypoint", "runtime", "schema"] as const;
const OPTIONAL_LOCK_KEYS = new Set([...LOCK_KEYS, "allow"]);
const DEPENDENCY_KEYS = ["dependencies", "files", "integrity", "license", "main", "name", "runtime", "source", "version"] as const;
const FILE_KEYS = ["bytes", "path", "sha256"] as const;

export interface JavaScriptDependencyFile {
  readonly path: string;
  readonly sha256: string;
  readonly bytes: number;
}

export interface JavaScriptDependency {
  readonly name: string;
  readonly version: string;
  readonly main: string;
  readonly integrity: string;
  readonly source: "registry" | "workspace" | "bundled";
  readonly license: string;
  readonly runtime: "poison-mjs-1";
  readonly dependencies: readonly string[];
  readonly files: readonly JavaScriptDependencyFile[];
}

export interface JavaScriptLock {
  readonly schema: "poison.javascript.lock/v1";
  readonly runtime: "poison-mjs-1";
  readonly entrypoint: string;
  readonly dependencies: readonly JavaScriptDependency[];
  readonly allow?: readonly "@flipperdevices/fz-sdk"[];
}

export interface DependencyImportFile {
  readonly path: string;
  readonly data: Uint8Array;
}

function object(value: unknown): Record<string, unknown> | null {
  return typeof value === "object" && value !== null && !Array.isArray(value)
    ? value as Record<string, unknown>
    : null;
}

function exactKeys(value: Record<string, unknown>, keys: readonly string[]): boolean {
  return Object.keys(value).length === keys.length && keys.every((key) => Object.hasOwn(value, key));
}

function safeSourcePath(path: unknown): path is string {
  return typeof path === "string" && path.length <= 160 && SOURCE_PATH.test(path) &&
    path.split("/").every((segment) => segment !== "." && segment !== "..");
}

function integrityBytes(value: unknown): Uint8Array {
  if (typeof value !== "string" || !/^sha256-[A-Za-z0-9+/]{43}=$/.test(value)) {
    throw new Error("dependency integrity must be an exact SHA-256 digest");
  }
  let decoded: string;
  try { decoded = atob(value.slice(7)); } catch { throw new Error("dependency integrity is invalid base64"); }
  if (decoded.length !== 32) throw new Error("dependency integrity must contain 32 bytes");
  return Uint8Array.from(decoded, (character) => character.charCodeAt(0));
}

function validateGraph(dependencies: readonly JavaScriptDependency[]): void {
  const byName = new Map(dependencies.map((dependency) => [dependency.name, dependency]));
  for (const dependency of dependencies) {
    for (const required of dependency.dependencies) {
      if (!byName.has(required)) throw new Error(`dependency ${dependency.name} references unknown dependency ${required}`);
    }
  }
  const visiting = new Set<string>();
  const visited = new Set<string>();
  const visit = (name: string, depth: number): void => {
    if (depth > MAX_GRAPH_DEPTH) throw new Error(`dependency graph exceeds depth ${MAX_GRAPH_DEPTH}`);
    if (visiting.has(name)) throw new Error(`dependency graph contains a cycle at ${name}`);
    if (visited.has(name)) return;
    visiting.add(name);
    for (const required of byName.get(name)?.dependencies ?? []) visit(required, depth + 1);
    visiting.delete(name);
    visited.add(name);
  };
  for (const name of [...byName.keys()].sort()) visit(name, 1);
}

export function validateJavaScriptLock(source: string): JavaScriptLock {
  let raw: unknown;
  try { raw = JSON.parse(source); } catch { throw new Error("dependency lock is not valid JSON"); }
  const lock = object(raw);
  if (!lock || !LOCK_KEYS.every((key) => Object.hasOwn(lock, key)) ||
      Object.keys(lock).some((key) => !OPTIONAL_LOCK_KEYS.has(key)) ||
      lock.schema !== SCHEMA || lock.runtime !== RUNTIME || !safeSourcePath(lock.entrypoint) ||
      !/\.(?:js|mjs|cjs)$/.test(lock.entrypoint) ||
      !Array.isArray(lock.dependencies) || lock.dependencies.length > MAX_DEPENDENCIES ||
      (lock.allow !== undefined && (!Array.isArray(lock.allow) || lock.allow.length > 32 ||
        lock.allow.some((entry) => entry !== "@flipperdevices/fz-sdk")))) {
    throw new Error("dependency lock contract is invalid");
  }
  const dependencies: JavaScriptDependency[] = [];
  const names = new Set<string>();
  let totalFiles = 0;
  for (const candidate of lock.dependencies) {
    const dependency = object(candidate);
    if (!dependency || !exactKeys(dependency, DEPENDENCY_KEYS) ||
        typeof dependency.name !== "string" || !NAME.test(dependency.name) || names.has(dependency.name) ||
        typeof dependency.version !== "string" || !VERSION.test(dependency.version) ||
        !["registry", "workspace", "bundled"].includes(String(dependency.source)) ||
        typeof dependency.license !== "string" || !LICENSE.test(dependency.license) || dependency.runtime !== RUNTIME ||
        !Array.isArray(dependency.dependencies) || dependency.dependencies.length > MAX_DEPENDENCIES ||
        dependency.dependencies.some((name) => typeof name !== "string" || !NAME.test(name)) ||
        new Set(dependency.dependencies).size !== dependency.dependencies.length ||
        !Array.isArray(dependency.files) || dependency.files.length < 1 || dependency.files.length > MAX_FILES) {
      throw new Error("dependency identity, license, runtime, graph, or files are invalid");
    }
    integrityBytes(dependency.integrity);
    const paths = new Set<string>();
    const files: JavaScriptDependencyFile[] = dependency.files.map((candidateFile) => {
      const file = object(candidateFile);
      if (!file || !exactKeys(file, FILE_KEYS) || !safeSourcePath(file.path) || paths.has(file.path) ||
          typeof file.sha256 !== "string" || !DIGEST.test(file.sha256) ||
          typeof file.bytes !== "number" || !Number.isSafeInteger(file.bytes) || file.bytes < 0 || file.bytes > MAX_FILE_BYTES) {
        throw new Error(`dependency ${dependency.name} has invalid file metadata`);
      }
      paths.add(file.path);
      return { path: file.path, sha256: file.sha256, bytes: file.bytes };
    });
    if (!safeSourcePath(dependency.main) || !/\.(?:js|mjs|cjs)$/.test(dependency.main) ||
        !paths.has(dependency.main)) {
      throw new Error(`dependency ${dependency.name} main must name an executable file in its inventory`);
    }
    totalFiles += files.length;
    names.add(dependency.name);
    dependencies.push({
      name: dependency.name,
      version: dependency.version,
      main: dependency.main,
      integrity: dependency.integrity as string,
      source: dependency.source as JavaScriptDependency["source"],
      license: dependency.license,
      runtime: RUNTIME,
      dependencies: [...dependency.dependencies] as string[],
      files,
    });
  }
  if (totalFiles > MAX_FILES) throw new Error(`dependency file count exceeds ${MAX_FILES}`);
  validateGraph(dependencies);
  return {
    schema: SCHEMA,
    runtime: RUNTIME,
    entrypoint: lock.entrypoint,
    dependencies,
    ...(lock.allow === undefined ? {} : { allow: [...lock.allow] as "@flipperdevices/fz-sdk"[] }),
  };
}

function bytesEqual(left: Uint8Array, right: Uint8Array): boolean {
  return left.byteLength === right.byteLength && left.every((value, index) => value === right[index]);
}

export async function importDependencyBundle(
  lockSource: string,
  files: readonly DependencyImportFile[],
): Promise<Readonly<Record<string, string>>> {
  const lock = validateJavaScriptLock(lockSource);
  const supplied = new Map<string, Uint8Array>();
  for (const file of files) {
    if (!safeSourcePath(file.path) || supplied.has(file.path)) throw new Error("dependency import contains an invalid or duplicate path");
    supplied.set(file.path, file.data);
  }
  const expected = new Set<string>();
  const imported: Record<string, string> = {};
  const decoder = new TextDecoder("utf-8", { fatal: true });
  let totalBytes = 0;
  for (const dependency of [...lock.dependencies].sort((left, right) => left.name < right.name ? -1 : left.name > right.name ? 1 : 0)) {
    const chunks: Uint8Array[] = [];
    for (const descriptor of [...dependency.files].sort((left, right) => left.path < right.path ? -1 : left.path > right.path ? 1 : 0)) {
      const path = `vendor/${dependency.name}/${dependency.version}/${descriptor.path}`;
      expected.add(path);
      const data = supplied.get(path);
      if (!data || data.byteLength !== descriptor.bytes || await sha256Hex(data) !== descriptor.sha256) {
        throw new Error(`vendored dependency file digest mismatch: ${path}`);
      }
      chunks.push(new TextEncoder().encode(descriptor.path), new Uint8Array([0]), data);
      totalBytes += data.byteLength;
      if (totalBytes > MAX_TOTAL_BYTES) throw new Error(`vendored dependencies exceed ${MAX_TOTAL_BYTES} bytes`);
      try { imported[path] = decoder.decode(data); } catch { throw new Error(`vendored dependency is not UTF-8 source: ${path}`); }
    }
    const canonical = new Uint8Array(chunks.reduce((total, chunk) => total + chunk.byteLength, 0));
    let offset = 0;
    for (const chunk of chunks) { canonical.set(chunk, offset); offset += chunk.byteLength; }
    const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", canonical));
    if (!bytesEqual(digest, integrityBytes(dependency.integrity))) {
      throw new Error(`vendored dependency package digest mismatch: ${dependency.name}`);
    }
  }
  if (supplied.size !== expected.size || [...supplied.keys()].some((path) => !expected.has(path))) {
    throw new Error("vendored dependency inventory mismatch");
  }
  imported["poison-js.lock"] = `${JSON.stringify(lock, null, 2)}\n`;
  return imported;
}

export async function verifyProjectDependencies(
  lockSource: string | undefined,
  files: Readonly<Record<string, string>>,
): Promise<void> {
  if (lockSource === undefined) throw new Error("dependency lock is missing");
  const vendored = Object.entries(files)
    .filter(([path]) => path.startsWith("vendor/"))
    .map(([path, source]) => ({ path, data: new TextEncoder().encode(source) }));
  await importDependencyBundle(lockSource, vendored);
}

function selectedPath(file: File): string {
  const raw = file.webkitRelativePath || file.name;
  const vendor = raw.indexOf("vendor/");
  return vendor < 0 ? raw : raw.slice(vendor);
}

export function DependencyManager({
  lockSource,
  onImport,
}: {
  readonly lockSource?: string;
  readonly onImport: (files: Readonly<Record<string, string>>) => void;
}): ReactElement {
  const [draft, setDraft] = useState(lockSource ?? "");
  const [selected, setSelected] = useState<readonly File[]>([]);
  const [status, setStatus] = useState<string>("");
  useEffect(() => setDraft(lockSource ?? ""), [lockSource]);
  const lock = useMemo(() => {
    if (!draft) return null;
    try { return validateJavaScriptLock(draft); } catch { return null; }
  }, [draft]);
  const loadLock = (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.currentTarget.files?.[0];
    if (!file) return;
    void file.text().then(setDraft, () => setStatus("Dependency lock could not be read"));
  };
  const submit = async () => {
    setStatus("Verifying dependency bytes");
    try {
      const files = await Promise.all(selected.map(async (file) => ({
        path: selectedPath(file),
        data: new Uint8Array(await file.arrayBuffer()),
      })));
      const imported = await importDependencyBundle(draft, files);
      onImport(imported);
      setStatus(`Imported ${Object.keys(imported).length - 1} immutable dependency files`);
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error));
    }
  };
  return <section aria-label="Offline dependencies">
    <h3>Offline dependencies</h3>
    <label>Dependency lock file<input aria-label="Dependency lock file" type="file" accept="application/json,.lock" onChange={loadLock} /></label>
    <label>Select dependency folder<input
      aria-label="Select dependency folder"
      type="file"
      multiple
      ref={(input) => { input?.setAttribute("webkitdirectory", ""); }}
      onChange={(event) => setSelected(Array.from(event.currentTarget.files ?? []))}
    /></label>
    <button type="button" disabled={!lock || selected.length === 0} onClick={() => void submit()}>Import verified dependencies</button>
    {lock ? <ul>{lock.dependencies.map((dependency) => <li key={dependency.name}>{dependency.name} {dependency.version} · {dependency.license}</li>)}</ul> : <p>No valid dependency lock loaded.</p>}
    {status && <p role="status">{status}</p>}
  </section>;
}
