import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { PolicyDecisionSchema, Role } from "../generated/poison_policy_pb";
import { PolicyClient } from "./PolicyClient";

describe("PolicyClient", () => {
  it("uses the registered encrypted PB_Main policy operation", async () => {
    const requests: Main[] = [];
    const client = new PolicyClient({
      async request(message) {
        requests.push(message);
        return create(MainSchema, {
          commandId: message.commandId,
          commandStatus: CommandStatus.OK,
          content: {
            case: "poisonPolicyDecision",
            value: create(PolicyDecisionSchema, {
              grantedCapabilities: 0x03,
              allowed: true,
              policyVersion: 1,
            }),
          },
        });
      },
    });

    const decision = await client.evaluate(Role.OPERATOR, 0x03, false, true);
    expect(requests).toHaveLength(1);
    const request = requests[0];
    expect(request.content.case).toBe("poisonPolicyRequest");
    expect(request.content.case === "poisonPolicyRequest" && request.content.value.role).toBe(Role.OPERATOR);
    expect(decision.allowed).toBe(true);
  });

  it("rejects malformed decisions instead of treating them as authorization", async () => {
    const client = new PolicyClient({
      async request(message) {
        return create(MainSchema, {
          commandId: message.commandId,
          commandStatus: CommandStatus.OK,
          content: {
            case: "poisonPolicyDecision",
            value: create(PolicyDecisionSchema, {
              grantedCapabilities: 0x80,
              allowed: true,
              policyVersion: 1,
            }),
          },
        });
      },
    });
    await expect(client.evaluate(Role.OPERATOR, 0x01, false, true)).rejects.toThrow(
      "device returned an invalid policy decision",
    );
  });
});
