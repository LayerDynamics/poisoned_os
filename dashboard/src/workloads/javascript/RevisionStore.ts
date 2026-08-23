import { openPoisonDatabase } from "../../offline/Database";
import type { JavaScriptManifest } from "./manifest";

export interface ProjectRevision { id: string; projectId: string; revision: number; digest: string; manifest: JavaScriptManifest; files: Readonly<Record<string, string>>; }
export type RevisionConflict = { kind: "conflict"; expectedRevision: number; actualRevision: number };

const MEMORY = new Map<string, ProjectRevision[]>();
const DIGEST = /^[0-9a-f]{64}$/;

function cloneRevision(value: ProjectRevision): ProjectRevision {
  return { ...value, files: { ...value.files }, manifest: { ...value.manifest, capabilities: [...value.manifest.capabilities], limits: { ...value.manifest.limits } } };
}

function validateRevision(value: ProjectRevision): void {
  if (!value.projectId || !Number.isSafeInteger(value.revision) || value.revision < 1 || !DIGEST.test(value.digest)) throw new Error("invalid project revision");
  for (const [path, source] of Object.entries(value.files)) if (!path || path.includes("..") || typeof source !== "string" || source.length > 1024 * 1024) throw new Error("invalid project file");
}

export async function saveProjectRevision(value: ProjectRevision): Promise<ProjectRevision | RevisionConflict> {
  validateRevision(value);
  const revision = cloneRevision(value);
  const current = MEMORY.get(value.projectId) ?? [];
  const latest = current.at(-1);
  if (latest && value.revision !== latest.revision + 1) return { kind: "conflict", expectedRevision: latest.revision + 1, actualRevision: value.revision };
  MEMORY.set(value.projectId, [...current, revision]);
  if (typeof indexedDB !== "undefined") {
    const db = await openPoisonDatabase();
    await new Promise<void>((resolve, reject) => { const request = db.transaction("projects", "readwrite").objectStore("projects").put(revision); request.onerror = () => reject(request.error ?? new Error("revision save failed")); request.onsuccess = () => resolve(); });
    db.close();
  }
  return cloneRevision(revision);
}

export function listProjectRevisions(projectId: string): ProjectRevision[] { return (MEMORY.get(projectId) ?? []).map(cloneRevision); }
export function latestProjectRevision(projectId: string): ProjectRevision | null {
  const revisions = MEMORY.get(projectId) ?? [];
  return revisions.length === 0 ? null : cloneRevision(revisions[revisions.length - 1]);
}

export async function loadProjectRevisions(projectId: string): Promise<ProjectRevision[]> {
  if (!projectId) throw new Error("project id is required");
  if (typeof indexedDB === "undefined") return listProjectRevisions(projectId);
  const db = await openPoisonDatabase();
  try {
    const records = await new Promise<unknown[]>((resolve, reject) => {
      const request = db.transaction("projects", "readonly").objectStore("projects").getAll();
      request.onerror = () => reject(request.error ?? new Error("revision load failed"));
      request.onsuccess = () => resolve(request.result as unknown[]);
    });
    const revisions = records
      .filter((record): record is ProjectRevision => typeof record === "object" && record !== null &&
        (record as Partial<ProjectRevision>).projectId === projectId)
      .map((record) => { validateRevision(record); return cloneRevision(record); })
      .sort((left, right) => left.revision - right.revision);
    for (const [index, revision] of revisions.entries()) {
      if (revision.revision !== index + 1) throw new Error("persisted project revision sequence is invalid");
    }
    if (revisions.length > 0) MEMORY.set(projectId, revisions.map(cloneRevision));
    return revisions.length > 0 ? revisions.map(cloneRevision) : listProjectRevisions(projectId);
  } finally {
    db.close();
  }
}

export function restoreProjectRevision(projectId: string, revision: number): ProjectRevision {
  const value = (MEMORY.get(projectId) ?? []).find((item) => item.revision === revision);
  if (!value) throw new Error("project revision not found");
  return cloneRevision(value);
}
