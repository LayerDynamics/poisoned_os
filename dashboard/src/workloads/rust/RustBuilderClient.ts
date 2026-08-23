export interface NativeArtifactManifest {
  readonly target: string;
  readonly api_version: number;
  readonly abi_version: number;
  readonly entry: string;
  readonly imports: readonly string[];
  readonly relocations: readonly number[];
  readonly capabilities: readonly string[];
  readonly digest: string;
}

export interface NativeArtifactAdmission {
  readonly accepted: boolean;
  readonly target?: string;
  readonly apiVersion?: number;
  readonly abiVersion?: number;
  readonly digest?: string;
  readonly error?: string;
}

export interface RustBuildJob {
  readonly id: string;
  readonly state: "created" | "inputs-finalized" | "running" | "succeeded" | "failed" | "cancelled" | "expired";
  readonly source_bytes: number;
  readonly log_bytes: number;
  readonly has_artifact: boolean;
}

export interface RustBuildProvenance {
  readonly source_bytes: number;
  readonly source_digest: string;
  readonly lock_digest: string;
  readonly toolchain_digest: string;
  readonly api_version: number;
  readonly target: string;
}

export class RustBuilderClient {
  public constructor(private readonly baseUrl: string, private readonly originToken: string) {
    if (!/^https?:\/\/localhost(?::\d+)?$/.test(baseUrl) && !/^https?:\/\/127\.0\.0\.1(?::\d+)?$/.test(baseUrl)) throw new Error("builder endpoint must be loopback");
    if (!originToken) throw new Error("builder origin token is required");
  }

  public async validateNativeArtifact(manifest: NativeArtifactManifest, signal?: AbortSignal): Promise<NativeArtifactAdmission> {
    const response = await fetch(`${this.baseUrl}/v1/rust/artifacts/validate`, {
      method: "POST",
      headers: { "content-type": "application/json", "x-poison-origin-token": this.originToken },
      body: JSON.stringify(manifest),
      signal,
    });
    const body = await response.json() as NativeArtifactAdmission;
    if (!response.ok || body.accepted !== true) throw new Error(body.error ?? `builder rejected artifact (${response.status})`);
    return body;
  }

  public async createJob(idempotencyKey: string, signal?: AbortSignal): Promise<string> {
    if (!/^[A-Za-z0-9._-]{1,64}$/.test(idempotencyKey)) throw new Error("invalid builder idempotency key");
    const response = await this.request("/v1/rust/build/jobs", "POST", { idempotency_key: idempotencyKey }, signal);
    const body = await response.json() as { id?: string };
    if (!response.ok || !body.id) throw new Error(`builder job creation failed (${response.status})`);
    return body.id;
  }

  public async finalizeJob(id: string, provenance: RustBuildProvenance, signal?: AbortSignal): Promise<void> {
    if (!Number.isSafeInteger(provenance.source_bytes) || provenance.source_bytes < 0) throw new Error("invalid builder source size");
    await this.expectNoContent(`/v1/rust/build/jobs/${encodeURIComponent(id)}/finalize`, "POST", provenance, signal);
  }

  public async startJob(id: string, signal?: AbortSignal): Promise<void> {
    await this.expectNoContent(`/v1/rust/build/jobs/${encodeURIComponent(id)}/start`, "POST", undefined, signal);
  }

  public async finishNativeJob(id: string, manifest: NativeArtifactManifest, outputBytes: number, signal?: AbortSignal): Promise<void> {
    if (!Number.isSafeInteger(outputBytes) || outputBytes < 1) throw new Error("invalid builder output size");
    await this.expectNoContent(`/v1/rust/build/jobs/${encodeURIComponent(id)}/finish`, "POST", { ...manifest, output_bytes: outputBytes }, signal);
  }

  public async status(id: string, signal?: AbortSignal): Promise<RustBuildJob> {
    const response = await this.request(`/v1/rust/build/jobs/${encodeURIComponent(id)}`, "GET", undefined, signal);
    const body = await response.json() as RustBuildJob;
    if (!response.ok || !body.id || !body.state) throw new Error(`builder status failed (${response.status})`);
    return body;
  }

  public async cancelJob(id: string, signal?: AbortSignal): Promise<void> {
    await this.expectNoContent(`/v1/rust/build/jobs/${encodeURIComponent(id)}/cancel`, "POST", undefined, signal);
  }

  private async request(path: string, method: "GET" | "POST", body: unknown, signal?: AbortSignal): Promise<Response> {
    return fetch(`${this.baseUrl}${path}`, { method, headers: { "content-type": "application/json", "x-poison-origin-token": this.originToken }, body: body === undefined ? undefined : JSON.stringify(body), signal });
  }

  private async expectNoContent(path: string, method: "POST", body: unknown, signal?: AbortSignal): Promise<void> {
    const response = await this.request(path, method, body, signal);
    if (!response.ok) throw new Error(`builder request failed (${response.status})`);
  }
}

export function validateNativeArtifactManifest(manifest: NativeArtifactManifest): void {
  if (manifest.target !== "thumbv7em-none-eabihf" || manifest.api_version !== 1 || manifest.abi_version !== 1 || manifest.entry !== "poison_rust_entry" || !/^[0-9a-f]{64}$/.test(manifest.digest)) throw new Error("invalid native artifact manifest");
  if (manifest.imports.some((name) => !name.startsWith("poison_")) || manifest.relocations.some((relocation) => ![0, 2, 3, 10].includes(relocation)) || manifest.capabilities.some((capability) => capability.length === 0 || capability.length > 64)) throw new Error("invalid native artifact admission fields");
}
