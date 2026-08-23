import { describe, expect, it } from "vitest";
import { validateConfirmationRequest } from "./ConfirmationDialog";

const digest = "00".repeat(32);

describe("ConfirmationDialog", () => {
  it("rejects changed or unbound confirmation requests", () => {
    expect(() => validateConfirmationRequest({ sessionId: 0n, role: 1, commandDigest: digest, targetDigest: digest, consequenceDigest: digest, policyVersion: 1, consequence: "erase", physicalRequired: true })).toThrow();
    expect(() => validateConfirmationRequest({ sessionId: 1n, role: 1, commandDigest: "bad", targetDigest: digest, consequenceDigest: digest, policyVersion: 1, consequence: "erase", physicalRequired: true })).toThrow();
    expect(() => validateConfirmationRequest({ sessionId: 1n, role: 1, commandDigest: digest, targetDigest: digest, consequenceDigest: digest, policyVersion: 1, consequence: "erase", physicalRequired: true })).not.toThrow();
  });
});
