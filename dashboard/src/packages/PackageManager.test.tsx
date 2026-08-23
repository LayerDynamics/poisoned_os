import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { PackageLifecycleState, PackageOperation, PackageOperationStatusSchema } from "../generated/poison_packages_pb";
import { canPackageTransition, createPackageRequest, packageStatusToRecord, type PackageDraft, type PackageRecord } from "./PackageManager";

const draft: PackageDraft = {
  packageId: "org.poison.test", version: "2.0.0", previousVersion: "1.0.0",
  manifestPath: "/ext/apps/.staging/org.poison.test/package.poison",
  candidateDigest: "a".repeat(64), previousDigest: "b".repeat(64), signingKeyId: "package-prod-1",
  capabilityMask: 0x88n, releaseSequence: 2, contentBytes: 4096,
  previousState: PackageLifecycleState.ACTIVE, protectedPackage: false, confirmationRequired: true,
};

describe("PackageManager", () => {
  it("keeps protected known-good packages removable only by policy", () => {
    const item: PackageRecord = { id: "safe", version: "1.0.0", signer: "release", capabilityMask: 0n, state: "active", protectedPackage: true };
    expect(canPackageTransition(item, "removed")).toBe(false);
    expect(canPackageTransition(item, "disabled")).toBe(true);
    expect(canPackageTransition({ ...item, protectedPackage: false, state: "quarantined" }, "removed")).toBe(true);
  });

  it("creates the real bounded PB_Main package operation request", () => {
    const token = new Uint8Array(16).fill(7);
    const request = createPackageRequest(41, PackageOperation.ACTIVATE, draft, token);
    expect(request.commandId).toBe(41);
    expect(request.content.case).toBe("poisonPackageOperationRequest");
    if (request.content.case !== "poisonPackageOperationRequest") throw new Error("wrong request");
    expect(request.content.value.packageId).toBe(draft.packageId);
    expect(request.content.value.candidateDigest).toBe(draft.candidateDigest);
    expect(request.content.value.capabilityMask).toBe(0x88n);
    expect(request.content.value.confirmationToken).toEqual(token);
  });

  it("uses device status as the package inventory source", () => {
    const status = create(PackageOperationStatusSchema, {
      packageId: draft.packageId, version: draft.version, signingKeyId: draft.signingKeyId,
      capabilityMask: draft.capabilityMask, state: PackageLifecycleState.QUARANTINED,
      protectedPackage: true, result: "verification-failed",
    });
    expect(packageStatusToRecord(status)).toEqual({
      id: draft.packageId, version: draft.version, signer: draft.signingKeyId,
      capabilityMask: draft.capabilityMask, state: "quarantined", protectedPackage: true,
    });
  });
});
