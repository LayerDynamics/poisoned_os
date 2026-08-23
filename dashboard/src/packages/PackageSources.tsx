import { create } from "@bufbuild/protobuf";
import type { ReactElement } from "react";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import {
  PackageCatalogFreshness,
  PackageCatalogRequestSchema,
  PackageCatalogSource,
  PackageCatalogState,
} from "../generated/poison_packages_pb";

export type PackageSourceKind = "device" | "bundled" | "imported" | "repository";
export type PackageFreshness = "unknown" | "fresh" | "stale" | "missing";
export type PackageAvailabilityState =
  | "installed"
  | "staged"
  | "available"
  | "incompatible"
  | "disabled"
  | "quarantined"
  | "revoked"
  | "rollback-candidate";

export interface PackageAvailability {
  id: string;
  version: string;
  signer: string;
  digest: string;
  source: PackageSourceKind;
  sourcePath: string;
  freshness: PackageFreshness;
  state: PackageAvailabilityState;
  verified: boolean;
  signerRevoked: boolean;
  conflicted: boolean;
  compatible: boolean;
  capabilityMask: bigint;
}

export function isInstallablePackage(record: PackageAvailability): boolean {
  return record.verified && record.compatible && !record.signerRevoked && !record.conflicted &&
    record.freshness === "fresh" && record.state === "available";
}

export interface PackageCatalogSession {
  requestStream(request: Main, maxResponses?: number, signal?: AbortSignal): Promise<readonly Main[]>;
}

export interface PackageCatalogSnapshot {
  readonly packages: readonly PackageAvailability[];
  readonly generation: number;
}

export class PackageCatalogClientError extends Error {
  public constructor(message: string) { super(message); this.name = "PackageCatalogClientError"; }
}

const SOURCES: Readonly<Record<number, PackageSourceKind>> = {
  [PackageCatalogSource.DEVICE_STORAGE]: "device",
  [PackageCatalogSource.BUNDLED_RELEASE]: "bundled",
  [PackageCatalogSource.IMPORTED_FILE]: "imported",
  [PackageCatalogSource.LOCAL_REPOSITORY]: "repository",
};
const FRESHNESS: Readonly<Record<number, PackageFreshness>> = {
  [PackageCatalogFreshness.UNKNOWN]: "unknown",
  [PackageCatalogFreshness.FRESH]: "fresh",
  [PackageCatalogFreshness.STALE]: "stale",
  [PackageCatalogFreshness.MISSING]: "missing",
};
const STATES: Readonly<Record<number, PackageAvailabilityState>> = {
  [PackageCatalogState.INSTALLED]: "installed",
  [PackageCatalogState.STAGED]: "staged",
  [PackageCatalogState.AVAILABLE]: "available",
  [PackageCatalogState.INCOMPATIBLE]: "incompatible",
  [PackageCatalogState.DISABLED]: "disabled",
  [PackageCatalogState.QUARANTINED]: "quarantined",
  [PackageCatalogState.REVOKED]: "revoked",
  [PackageCatalogState.ROLLBACK_CANDIDATE]: "rollback-candidate",
};

function validText(value: string, maximum: number): boolean {
  return value.length > 0 && value.length <= maximum && /^[\x20-\x7e]+$/.test(value);
}

export class PackageCatalogClient {
  private nextCommandId = 30_000;
  public constructor(private readonly session: PackageCatalogSession) {}

  public async readPage(offset = 0, maxRecords = 32, signal?: AbortSignal): Promise<{
    packages: readonly PackageAvailability[]; generation: number; totalRecords: number; nextOffset: number;
  }> {
    if (!Number.isInteger(offset) || offset < 0 || !Number.isInteger(maxRecords) || maxRecords < 1 || maxRecords > 32) {
      throw new PackageCatalogClientError("package catalog request is invalid");
    }
    const responses = await this.session.requestStream(create(MainSchema, {
      commandId: this.nextCommandId++,
      content: { case: "poisonPackageCatalogRequest", value: create(PackageCatalogRequestSchema, { offset, maxRecords }) },
    }), maxRecords + 1, signal);
    const packages: PackageAvailability[] = [];
    let generation: number | null = null;
    let end: Extract<Main["content"], { case: "poisonPackageCatalogEnd" }>["value"] | null = null;
    for (const [index, response] of responses.entries()) {
      if (response.commandStatus !== CommandStatus.OK) throw new PackageCatalogClientError("device rejected package catalog request");
      if (response.content.case === "poisonPackageCatalogRecord" && response.hasNext && !end) {
        const record = response.content.value;
        const source = SOURCES[record.source];
        const freshness = FRESHNESS[record.freshness];
        const state = STATES[record.state];
        if (!source || !freshness || !state || record.ordinal !== offset + packages.length ||
            !validText(record.id, 64) || !validText(record.version, 32) ||
            !validText(record.signer, 64) || !validText(record.sourcePath, 256) ||
            !/^[0-9a-f]{64}$/.test(record.digest) ||
            (generation !== null && generation !== record.generation)) {
          throw new PackageCatalogClientError("device returned an invalid package catalog record");
        }
        generation = record.generation;
        packages.push({
          id: record.id, version: record.version, signer: record.signer, digest: record.digest,
          source, sourcePath: record.sourcePath, freshness, state, verified: record.verified,
          signerRevoked: record.signerRevoked, conflicted: record.conflicted,
          compatible: record.compatible, capabilityMask: record.capabilityMask,
        });
      } else if (response.content.case === "poisonPackageCatalogEnd" && !response.hasNext && index === responses.length - 1) {
        end = response.content.value;
      } else {
        throw new PackageCatalogClientError("device returned a malformed package catalog stream");
      }
    }
    if (!end || end.returnedRecords !== packages.length || end.nextOffset !== offset + packages.length ||
        end.totalRecords < end.nextOffset || (generation !== null && generation !== end.generation)) {
      throw new PackageCatalogClientError("device returned an invalid package catalog receipt");
    }
    return { packages, generation: end.generation, totalRecords: end.totalRecords, nextOffset: end.nextOffset };
  }

  public async readAll(signal?: AbortSignal): Promise<PackageCatalogSnapshot> {
    const packages: PackageAvailability[] = [];
    let offset = 0;
    let generation: number | null = null;
    let total = 0;
    do {
      const page = await this.readPage(offset, 32, signal);
      if (generation !== null && generation !== page.generation) {
        throw new PackageCatalogClientError("package catalog changed during pagination");
      }
      generation = page.generation;
      total = page.totalRecords;
      packages.push(...page.packages);
      if (page.nextOffset <= offset && page.nextOffset < total) {
        throw new PackageCatalogClientError("package catalog pagination did not advance");
      }
      offset = page.nextOffset;
    } while (offset < total);
    if (packages.length !== total) throw new PackageCatalogClientError("package catalog receipt count does not match inventory");
    return { packages, generation: generation ?? 0 };
  }
}

export function PackageSources({ packages, onInstall }: {
  packages: readonly PackageAvailability[];
  onInstall?: (record: PackageAvailability) => void;
}): ReactElement {
  return <ul aria-label="Package sources">
    {packages.map((record) => {
      const installable = isInstallablePackage(record);
      return <li key={`${record.id}:${record.version}:${record.source}:${record.sourcePath}`}>
        <span>{record.id} {record.version} · {record.state} · {record.source}</span>
        <small>{record.signer} · {record.freshness} · capabilities {record.capabilityMask.toString()}{record.conflicted ? " · conflict" : ""}</small>
        <button type="button" disabled={!installable} onClick={() => onInstall?.(record)}>Install</button>
      </li>;
    })}
  </ul>;
}
