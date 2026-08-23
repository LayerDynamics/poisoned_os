import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import {
  PackageCatalogEndSchema,
  PackageCatalogFreshness,
  PackageCatalogRecordSchema,
  PackageCatalogSource,
  PackageCatalogState,
} from "../generated/poison_packages_pb";
import { isInstallablePackage, PackageCatalogClient, type PackageAvailability } from "./PackageSources";

const record: PackageAvailability = {
  id: "org.example", version: "1.0.0", signer: "release", digest: "0".repeat(64),
  source: "bundled", sourcePath: "/catalog/org.example", freshness: "fresh", state: "available",
  verified: true, signerRevoked: false, conflicted: false,
  compatible: true, capabilityMask: 3n,
};

describe("PackageSources", () => {
  it("only exposes fresh verified non-conflicting metadata as installable", () => {
    expect(isInstallablePackage(record)).toBe(true);
    expect(isInstallablePackage({ ...record, freshness: "stale" })).toBe(false);
    expect(isInstallablePackage({ ...record, conflicted: true })).toBe(false);
    expect(isInstallablePackage({ ...record, signerRevoked: true })).toBe(false);
    expect(isInstallablePackage({ ...record, compatible: false })).toBe(false);
  });

  it("validates and returns a complete device catalog stream", async () => {
    const requests: Main[] = [];
    const client = new PackageCatalogClient({
      async requestStream(request, maximum) {
        requests.push(request);
        expect(maximum).toBe(33);
        return [
          create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, hasNext: true, content: {
            case: "poisonPackageCatalogRecord", value: create(PackageCatalogRecordSchema, {
              id: record.id, version: record.version, signer: record.signer, digest: record.digest,
              source: PackageCatalogSource.BUNDLED_RELEASE, sourcePath: record.sourcePath,
              freshness: PackageCatalogFreshness.FRESH, state: PackageCatalogState.AVAILABLE,
              verified: true, compatible: true, capabilityMask: 3n, ordinal: 0, generation: 9,
            }),
          } }),
          create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, content: {
            case: "poisonPackageCatalogEnd", value: create(PackageCatalogEndSchema, {
              returnedRecords: 1, totalRecords: 1, nextOffset: 1, generation: 9,
            }),
          } }),
        ];
      },
    });
    const snapshot = await client.readAll();
    expect(snapshot).toEqual({ packages: [record], generation: 9 });
    expect(requests[0]?.content.case).toBe("poisonPackageCatalogRequest");
  });

  it("rejects catalog records with a mismatched ordinal", async () => {
    const client = new PackageCatalogClient({
      async requestStream(request) {
        return [
          create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, hasNext: true, content: {
            case: "poisonPackageCatalogRecord", value: create(PackageCatalogRecordSchema, {
              id: record.id, version: record.version, signer: record.signer, digest: record.digest,
              source: PackageCatalogSource.BUNDLED_RELEASE, sourcePath: record.sourcePath,
              freshness: PackageCatalogFreshness.FRESH, state: PackageCatalogState.AVAILABLE,
              verified: true, compatible: true, ordinal: 4, generation: 1,
            }),
          } }),
          create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, content: {
            case: "poisonPackageCatalogEnd", value: create(PackageCatalogEndSchema, {
              returnedRecords: 1, totalRecords: 1, nextOffset: 1, generation: 1,
            }),
          } }),
        ];
      },
    });
    await expect(client.readAll()).rejects.toThrow("invalid package catalog record");
  });
});
