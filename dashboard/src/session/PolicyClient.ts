import { create } from "@bufbuild/protobuf";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { PolicyRequestSchema, Role, type PolicyDecision } from "../generated/poison_policy_pb";

export interface PolicySession {
  request(request: Main, signal?: AbortSignal): Promise<Main>;
}

const POLICY_VERSION = 1;
const KNOWN_CAPABILITIES = 0xff;

export class PolicyClient {
  private nextCommandId = 2_000;

  public constructor(private readonly session: PolicySession) {}

  public async evaluate(
    role: Role,
    requestedCapabilities: number,
    deviceLocked: boolean,
    physicalConfirmation: boolean,
    signal?: AbortSignal,
  ): Promise<PolicyDecision> {
    if (!Number.isInteger(role) || role < Role.OWNER || role > Role.OBSERVER ||
        !Number.isInteger(requestedCapabilities) || requestedCapabilities < 1 ||
        (requestedCapabilities & ~KNOWN_CAPABILITIES) !== 0) {
      throw new Error("policy request is outside its bounds");
    }
    const response = await this.session.request(create(MainSchema, {
      commandId: this.nextCommandId++,
      content: {
        case: "poisonPolicyRequest",
        value: create(PolicyRequestSchema, {
          role,
          requestedCapabilities,
          deviceLocked,
          physicalConfirmation,
          policyVersion: POLICY_VERSION,
        }),
      },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonPolicyDecision" ||
        response.content.value.policyVersion !== POLICY_VERSION ||
        (response.content.value.grantedCapabilities & ~requestedCapabilities) !== 0 ||
        (response.content.value.allowed && response.content.value.grantedCapabilities !== requestedCapabilities) ||
        (!response.content.value.allowed && !response.content.value.denialReason)) {
      throw new Error("device returned an invalid policy decision");
    }
    return response.content.value;
  }
}
