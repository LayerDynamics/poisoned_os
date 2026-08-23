import type { JavaScriptManifest } from "./manifest";

export interface JavaScriptProject { manifest: JavaScriptManifest; files: Readonly<Record<string, string>>; }
export interface PackagedJavaScriptProject { manifest: JavaScriptManifest; files: readonly { path: string; source: string }[]; canonical: string; }

const PROJECT_PATH = /^(?:[a-zA-Z0-9@_-][a-zA-Z0-9@._+-]*\/)*[a-zA-Z0-9_-][a-zA-Z0-9._-]*\.(?:js|mjs|cjs|json)$/;

function projectMemberPath(path: string): boolean {
  return path === "poison-js.lock" || PROJECT_PATH.test(path);
}

function orderedFiles(files: Readonly<Record<string, string>>): readonly { path: string; source: string }[] {
  const ordered = Object.entries(files)
    .sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0)
    .map(([path, source]) => ({ path, source }));
  if (ordered.length === 0 || ordered.length > 32 ||
      ordered.some(({ path, source }) => !projectMemberPath(path) || path.length > 160 ||
        new TextEncoder().encode(source).byteLength > 256 * 1024)) {
    throw new Error("project contains an invalid JavaScript source file");
  }
  return ordered;
}

export function canonicalizeProject(project: JavaScriptProject): string {
  const files = orderedFiles(project.files);
  return JSON.stringify({ manifest: project.manifest, files });
}

export function packageJavaScriptProject(project: JavaScriptProject): PackagedJavaScriptProject {
  const files = orderedFiles(project.files);
  if (!files.some(({ path }) => path === project.manifest.entrypoint)) throw new Error("entrypoint is not present");
  return { manifest: project.manifest, files, canonical: JSON.stringify({ manifest: project.manifest, files }) };
}
