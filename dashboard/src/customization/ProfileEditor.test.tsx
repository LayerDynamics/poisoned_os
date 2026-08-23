import { create } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { CommandStatus, MainSchema } from "../generated/flipper_pb";
import { ProfileStatusSchema } from "../generated/poison_profiles_pb";
import { canApplyProfile, exportProfile, importProfile, ProfileDeviceClient, profileChangeSummary, type ProfileDraft } from "./ProfileEditor";
import { isAccessibleTheme, themeContrastRatioX10 } from "./ThemeEditor";

const draft: ProfileDraft = {
  id: "field", version: "1.0.0", role: "field", policyId: "builtin.field",
  enabledTools: ["nfc"], favorites: ["nfc"], hiddenTools: [], shortcuts: ["nfc"],
  themeId: "builtin.field", fontPackId: "builtin.default", iconPackId: "builtin.default", menuId: "builtin.field",
  dashboardLayout: "field-console", homePresentation: "builtin.field", statusPresentation: "builtin.field",
  lockBehavior: "pin", notificationsEnabled: true, hapticsEnabled: true, toolDefaultsJson: "{}",
  transportPolicy: "local-only", loggingPolicy: "metadata", evidencePolicy: "digest-only", radioRegion: "device",
  peripheralSafety: "guarded", classroomPolicy: "none", contrastRatioX10: 45, capabilityMask: 1n, classroomRestricted: false,
};
describe("ProfileEditor", () => {
  it("requires role capability intersection and reports changes", () => {
    expect(canApplyProfile(draft, 1n)).toBe(true);
    expect(canApplyProfile({ ...draft, capabilityMask: 2n }, 1n)).toBe(false);
    expect(profileChangeSummary(draft, { ...draft, themeId: "dark" })).toEqual(["themeId changed"]);
  });
  it("round-trips a canonical profile export without losing the capability mask", () => {
    expect(importProfile(exportProfile(draft))).toEqual(draft);
  });
  it("rejects malformed or inaccessible profile imports", () => {
    expect(() => importProfile("{}")).toThrow();
    expect(() => exportProfile({ ...draft, contrastRatioX10: 44 })).toThrow();
  });
  it("computes theme contrast from the actual colors", () => {
    const theme = { id: "builtin.field", foreground: "#ffffff", background: "#000000" };
    expect(themeContrastRatioX10(theme)).toBe(210);
    expect(isAccessibleTheme(theme)).toBe(true);
    expect(isAccessibleTheme({ ...theme, foreground: "#777777", background: "#888888" })).toBe(false);
  });
  it("previews and applies through the real generated profile messages", async () => {
    const requests: string[] = [];
    const client = new ProfileDeviceClient({
      async request(request) {
        requests.push(request.content.case ?? "missing");
        if (request.content.case === "poisonProfile") {
          expect(request.content.value.enabledTools).toEqual(["nfc"]);
          expect(request.content.value.shortcuts).toEqual(["nfc"]);
          return create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, content: { case: "poisonProfileStatus", value: create(ProfileStatusSchema, { profileId: draft.id, version: draft.version, preview: true, confirmationToken: new Uint8Array(16).fill(3) }) } });
        }
        if (request.content.case !== "poisonProfileApply") throw new Error("wrong request");
        expect(request.content.value.confirmationTokenBytes).toEqual(new Uint8Array(16).fill(3));
        return create(MainSchema, { commandId: request.commandId, commandStatus: CommandStatus.OK, content: { case: "poisonProfileStatus", value: create(ProfileStatusSchema, { profileId: draft.id, version: draft.version, preview: false }) } });
      },
    });
    await client.preview(draft);
    await client.apply();
    expect(requests).toEqual(["poisonProfile", "poisonProfileApply"]);
  });
});
