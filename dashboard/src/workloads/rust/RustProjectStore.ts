export type RustTarget = "native-fap" | "wasm";
export interface RustProjectRevision { projectId: string; revision: number; target: RustTarget; files: Readonly<Record<string, string>>; cargoLockDigest: string; }

export function validateRustProject(project: RustProjectRevision): void {
  if (!/^[a-z0-9._-]{1,64}$/.test(project.projectId) || !Number.isSafeInteger(project.revision) || project.revision < 1 || !["native-fap", "wasm"].includes(project.target) || !/^[0-9a-f]{64}$/.test(project.cargoLockDigest)) throw new Error("invalid Rust project revision");
  for (const [path, source] of Object.entries(project.files)) if (!/^(?:[a-zA-Z0-9._-]+\/)*[a-zA-Z0-9._-]+$/.test(path) || path.includes("..") || typeof source !== "string" || source.length > 1024 * 1024) throw new Error("invalid Rust project file");
}

export function canonicalRustProject(project: RustProjectRevision): string { validateRustProject(project); return JSON.stringify({ ...project, files: Object.entries(project.files).sort(([a], [b]) => a.localeCompare(b)) }); }
