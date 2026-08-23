import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import {
  ContentUpdateOperation,
  ContentUpdateState,
  ContentUpdateStatusSchema,
  ContentUpdateType,
} from "../generated/poison_packages_pb";
import { createUpdateRequest, type UpdateDraft } from "./UpdateManager";

const draft: UpdateDraft = {
  manifestPath: "/ext/update/poison/update.poison",
  candidateDigest: "0".repeat(64),
};

const deviceStatus = create(ContentUpdateStatusSchema, {
  updateId: "firmware-2",
  state: ContentUpdateState.AWAITING_CONFIRMATION,
  contentType: ContentUpdateType.FIRMWARE,
  candidateDigest: draft.candidateDigest,
  previousDigest: "a".repeat(64),
  releaseSequence: 2,
  receivedBytes: 8192,
  contentBytes: 8192,
  confirmationRequired: true,
  result: "verified",
  confirmationToken: new Uint8Array(16).fill(7),
});

describe("createUpdateRequest", () => {
  it("imports only the signed archive locator and digest", () => {
    const request = createUpdateRequest(41, ContentUpdateOperation.IMPORT, draft);
    expect(request.commandId).toBe(41);
    expect(request.content.case).toBe("poisonContentUpdateRequest");
    if (request.content.case !== "poisonContentUpdateRequest") return;
    expect(request.content.value).toMatchObject({
      operation: ContentUpdateOperation.IMPORT,
      updateId: "",
      manifestPath: draft.manifestPath,
      candidateDigest: draft.candidateDigest,
      previousDigest: "",
      releaseSequence: 0,
      contentBytes: 0,
    });
  });

  it("binds later operations to device-issued signed facts and token", () => {
    const token = new Uint8Array(16).fill(7);
    const request = createUpdateRequest(
      42,
      ContentUpdateOperation.ACTIVATE,
      draft,
      deviceStatus,
      token,
    );
    if (request.content.case !== "poisonContentUpdateRequest") throw new Error("wrong oneof");
    expect(request.content.value.updateId).toBe(deviceStatus.updateId);
    expect(request.content.value.contentType).toBe(deviceStatus.contentType);
    expect(request.content.value.confirmationToken).toEqual(token);
    expect(request.content.value.receivedBytes).toBe(0);
  });

  it("reports the device-derived archive size while staging", () => {
    const request = createUpdateRequest(43, ContentUpdateOperation.STAGE, draft, deviceStatus);
    if (request.content.case !== "poisonContentUpdateRequest") throw new Error("wrong oneof");
    expect(request.content.value.receivedBytes).toBe(deviceStatus.contentBytes);
  });

  it("queries boot health without claiming that the browser verified the boot", () => {
    const request = createUpdateRequest(44, ContentUpdateOperation.HEALTH, draft, deviceStatus);
    if (request.content.case !== "poisonContentUpdateRequest") throw new Error("wrong oneof");
    expect(request.content.value.healthy).toBe(false);
  });
});
