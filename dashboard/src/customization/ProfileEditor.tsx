import { create } from "@bufbuild/protobuf";
import { useRef, useState, type ReactElement } from "react";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { ProfileApplySchema, ProfileSchema, type ProfileStatus } from "../generated/poison_profiles_pb";

const encoder = new TextEncoder();
const identifier = /^[a-z0-9][a-z0-9._-]*$/;

export interface ProfileDraft {
  id: string;
  version: string;
  role: string;
  policyId: string;
  enabledTools: readonly string[];
  favorites: readonly string[];
  hiddenTools: readonly string[];
  shortcuts: readonly string[];
  themeId: string;
  fontPackId: string;
  iconPackId: string;
  menuId: string;
  dashboardLayout: string;
  homePresentation: string;
  statusPresentation: string;
  lockBehavior: string;
  notificationsEnabled: boolean;
  hapticsEnabled: boolean;
  toolDefaultsJson: string;
  transportPolicy: string;
  loggingPolicy: string;
  evidencePolicy: string;
  radioRegion: string;
  peripheralSafety: string;
  classroomPolicy: string;
  contrastRatioX10: number;
  capabilityMask: bigint;
  classroomRestricted: boolean;
}
export interface SerializedProfile extends Omit<ProfileDraft, "capabilityMask"> { format: 1; capabilityMask: string; }
export interface ProfileRequestClient { request(request: Main, signal?: AbortSignal): Promise<Main>; }

export function profileChangeSummary(before: ProfileDraft, after: ProfileDraft): readonly string[] {
  const changes: string[] = [];
  for (const key of ["role", "policyId", "themeId", "fontPackId", "iconPackId", "menuId", "dashboardLayout", "homePresentation", "statusPresentation", "lockBehavior", "notificationsEnabled", "hapticsEnabled", "toolDefaultsJson", "transportPolicy", "loggingPolicy", "evidencePolicy", "radioRegion", "peripheralSafety", "classroomPolicy", "contrastRatioX10", "classroomRestricted"] as const) if (before[key] !== after[key]) changes.push(`${key} changed`);
  if (before.capabilityMask !== after.capabilityMask) changes.push("capability mask changed");
  if (before.enabledTools.join("\0") !== after.enabledTools.join("\0")) changes.push("enabled tools changed");
  if (before.favorites.join("\0") !== after.favorites.join("\0")) changes.push("favorites changed");
  if (before.hiddenTools.join("\0") !== after.hiddenTools.join("\0")) changes.push("hidden tools changed");
  if (before.shortcuts.join("\0") !== after.shortcuts.join("\0")) changes.push("shortcuts changed");
  return changes;
}
export function canApplyProfile(profile: ProfileDraft, roleCapabilityMask: bigint): boolean {
  const ids = [profile.id, profile.policyId, profile.themeId, profile.fontPackId, profile.iconPackId, profile.menuId, profile.homePresentation, profile.statusPresentation, ...profile.enabledTools, ...profile.favorites, ...profile.hiddenTools, ...profile.shortcuts];
  const choicesValid = ["pin", "locked", "classroom"].includes(profile.lockBehavior) &&
    ["usb-only", "local-only", "classroom-managed"].includes(profile.transportPolicy) &&
    ["metadata", "detailed"].includes(profile.loggingPolicy) &&
    ["digest-only", "full-local"].includes(profile.evidencePolicy) &&
    ["guarded", "strict"].includes(profile.peripheralSafety) &&
    ["none", "student", "instructor"].includes(profile.classroomPolicy) &&
    (profile.radioRegion === "device" || /^[A-Z]{2}$/.test(profile.radioRegion));
  let defaultsValid = false;
  try { defaultsValid = typeof JSON.parse(profile.toolDefaultsJson) === "object" && JSON.parse(profile.toolDefaultsJson) !== null; }
  catch { defaultsValid = false; }
  return ids.every((value) => identifier.test(value) && encoder.encode(value).byteLength <= 64) &&
    encoder.encode(profile.version).byteLength > 0 && encoder.encode(profile.version).byteLength <= 32 &&
    encoder.encode(profile.role).byteLength > 0 && encoder.encode(profile.role).byteLength <= 32 &&
    encoder.encode(profile.dashboardLayout).byteLength > 0 && encoder.encode(profile.dashboardLayout).byteLength <= 256 &&
    encoder.encode(profile.toolDefaultsJson).byteLength >= 2 && encoder.encode(profile.toolDefaultsJson).byteLength <= 512 &&
    profile.enabledTools.length <= 32 && profile.favorites.length <= 32 && profile.hiddenTools.length <= 32 && profile.shortcuts.length <= 32 &&
    choicesValid && defaultsValid && (!profile.classroomRestricted || (profile.classroomPolicy !== "none" && profile.transportPolicy === "classroom-managed")) &&
    profile.contrastRatioX10 >= 45 && (profile.capabilityMask & ~roleCapabilityMask) === 0n;
}
export function exportProfile(profile: ProfileDraft): string {
  if (!canApplyProfile(profile, profile.capabilityMask)) throw new Error("invalid profile");
  const serialized: SerializedProfile = { format: 1, ...profile, capabilityMask: `0x${profile.capabilityMask.toString(16)}` };
  return JSON.stringify(serialized);
}
export function importProfile(encoded: string): ProfileDraft {
  const value: unknown = JSON.parse(encoded);
  if (!value || typeof value !== "object" || (value as Partial<SerializedProfile>).format !== 1) throw new Error("unsupported profile format");
  const profile = value as SerializedProfile;
  if (!Array.isArray(profile.enabledTools) || !Array.isArray(profile.favorites) || !Array.isArray(profile.hiddenTools) || !Array.isArray(profile.shortcuts) || !/^0x[0-9a-f]+$/i.test(profile.capabilityMask)) throw new Error("invalid profile");
  const { format: _format, capabilityMask, ...draft } = profile;
  const result = { ...draft, capabilityMask: BigInt(capabilityMask) };
  if (!canApplyProfile(result, result.capabilityMask)) throw new Error("invalid profile");
  return result;
}

export class ProfileDeviceClient {
  private commandId = 3000;
  private previewStatus: ProfileStatus | null = null;
  public constructor(private readonly session: ProfileRequestClient) {}
  public async preview(profile: ProfileDraft, signal?: AbortSignal): Promise<ProfileStatus> {
    if (!canApplyProfile(profile, 0xffn)) throw new Error("invalid profile");
    const response = await this.session.request(create(MainSchema, {
      commandId: this.commandId++,
      content: { case: "poisonProfile", value: create(ProfileSchema, {
        format: 1,
        id: profile.id,
        version: profile.version,
        role: profile.role,
        policyId: profile.policyId,
        enabledTools: [...profile.enabledTools],
        favorites: [...profile.favorites],
        hiddenTools: [...profile.hiddenTools],
        shortcuts: [...profile.shortcuts],
        themeId: profile.themeId,
        fontPackId: profile.fontPackId,
        iconPackId: profile.iconPackId,
        menuId: profile.menuId,
        dashboardLayout: profile.dashboardLayout,
        homePresentation: profile.homePresentation,
        statusPresentation: profile.statusPresentation,
        lockBehavior: profile.lockBehavior,
        notificationsEnabled: profile.notificationsEnabled,
        hapticsEnabled: profile.hapticsEnabled,
        toolDefaultsJson: profile.toolDefaultsJson,
        transportPolicy: profile.transportPolicy,
        loggingPolicy: profile.loggingPolicy,
        evidencePolicy: profile.evidencePolicy,
        radioRegion: profile.radioRegion,
        peripheralSafety: profile.peripheralSafety,
        classroomPolicy: profile.classroomPolicy,
        contrastRatioX10: profile.contrastRatioX10,
        capabilityMask: profile.capabilityMask,
        classroomRestricted: profile.classroomRestricted,
      }) },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonProfileStatus" ||
        !response.content.value.preview || response.content.value.confirmationToken.byteLength !== 16) throw new Error("device rejected profile preview");
    this.previewStatus = response.content.value;
    return response.content.value;
  }
  public async apply(signal?: AbortSignal): Promise<ProfileStatus> {
    if (!this.previewStatus) throw new Error("profile preview is required");
    const response = await this.session.request(create(MainSchema, {
      commandId: this.commandId++,
      content: { case: "poisonProfileApply", value: create(ProfileApplySchema, {
        profileId: this.previewStatus.profileId,
        confirmationToken: "",
        confirmationTokenBytes: this.previewStatus.confirmationToken,
      }) },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonProfileStatus" || response.content.value.preview) throw new Error("device rejected profile activation");
    this.previewStatus = null;
    return response.content.value;
  }
}

export function ProfileEditor({ draft, roleCapabilityMask, session }: { draft: ProfileDraft; roleCapabilityMask: bigint; session: ProfileRequestClient }): ReactElement {
  const [editing, setEditing] = useState(draft);
  const [previewed, setPreviewed] = useState(false);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState<string | null>(null);
  const [portableProfile, setPortableProfile] = useState("");
  const client = useRef(new ProfileDeviceClient(session));
  const changes = profileChangeSummary(draft, editing);
  const update = <K extends keyof ProfileDraft>(key: K, value: ProfileDraft[K]) => { setEditing((current) => ({ ...current, [key]: value })); setPreviewed(false); };
  const preview = async () => {
    setBusy(true); setMessage(null);
    try { await client.current.preview(editing); setPreviewed(true); setMessage("Preview accepted by device; review the changes before applying."); }
    catch (error) { setMessage(error instanceof Error ? error.message : String(error)); }
    finally { setBusy(false); }
  };
  const apply = async () => {
    setBusy(true); setMessage(null);
    try { await client.current.apply(); setPreviewed(false); setMessage("Profile applied atomically."); }
    catch (error) { setMessage(error instanceof Error ? error.message : String(error)); }
    finally { setBusy(false); }
  };
  const importPortable = () => {
    try { setEditing(importProfile(portableProfile)); setPreviewed(false); setMessage("Profile imported locally; preview it on the device before applying."); }
    catch (error) { setMessage(error instanceof Error ? error.message : String(error)); }
  };
  return <section className="profile-card" aria-label="Profile editor">
    <div className="update-heading"><div><p className="eyebrow">SIGNED APPEARANCE PROFILE</p><h2>{editing.id}</h2></div><output>{previewed ? "preview ready" : "editing"}</output></div>
    <div className="update-grid">
      <label>Role<input value={editing.role} onChange={(event) => update("role", event.target.value)} /></label>
      <label>Role policy<input value={editing.policyId} onChange={(event) => update("policyId", event.target.value)} /></label>
      <label>Enabled tools<input value={editing.enabledTools.join(",")} onChange={(event) => update("enabledTools", event.target.value.split(",").filter(Boolean))} /></label>
      <label>Hidden tools<input value={editing.hiddenTools.join(",")} onChange={(event) => update("hiddenTools", event.target.value.split(",").filter(Boolean))} /></label>
      <label>Favorites<input value={editing.favorites.join(",")} onChange={(event) => update("favorites", event.target.value.split(",").filter(Boolean))} /></label>
      <label>Shortcuts<input value={editing.shortcuts.join(",")} onChange={(event) => update("shortcuts", event.target.value.split(",").filter(Boolean))} /></label>
      <label>Theme package<input value={editing.themeId} onChange={(event) => update("themeId", event.target.value)} /></label>
      <label>Font package<input value={editing.fontPackId} onChange={(event) => update("fontPackId", event.target.value)} /></label>
      <label>Icon package<input value={editing.iconPackId} onChange={(event) => update("iconPackId", event.target.value)} /></label>
      <label>Menu<input value={editing.menuId} onChange={(event) => update("menuId", event.target.value)} /></label>
      <label>Dashboard layout<input value={editing.dashboardLayout} onChange={(event) => update("dashboardLayout", event.target.value)} /></label>
      <label>Home presentation<input value={editing.homePresentation} onChange={(event) => update("homePresentation", event.target.value)} /></label>
      <label>Status presentation<input value={editing.statusPresentation} onChange={(event) => update("statusPresentation", event.target.value)} /></label>
      <label>Lock behavior<select value={editing.lockBehavior} onChange={(event) => update("lockBehavior", event.target.value)}><option value="pin">PIN</option><option value="locked">Always locked</option><option value="classroom">Classroom</option></select></label>
      <label>Transport policy<select value={editing.transportPolicy} onChange={(event) => update("transportPolicy", event.target.value)}><option value="usb-only">USB only</option><option value="local-only">Local only</option><option value="classroom-managed">Classroom managed</option></select></label>
      <label>Logging policy<select value={editing.loggingPolicy} onChange={(event) => update("loggingPolicy", event.target.value)}><option value="metadata">Metadata</option><option value="detailed">Detailed</option></select></label>
      <label>Evidence policy<select value={editing.evidencePolicy} onChange={(event) => update("evidencePolicy", event.target.value)}><option value="digest-only">Digest only</option><option value="full-local">Full local</option></select></label>
      <label>Radio region<input value={editing.radioRegion} onChange={(event) => update("radioRegion", event.target.value)} /></label>
      <label>Peripheral safety<select value={editing.peripheralSafety} onChange={(event) => update("peripheralSafety", event.target.value)}><option value="guarded">Guarded</option><option value="strict">Strict</option></select></label>
      <label>Classroom policy<select value={editing.classroomPolicy} onChange={(event) => update("classroomPolicy", event.target.value)}><option value="none">None</option><option value="student">Student</option><option value="instructor">Instructor</option></select></label>
      <label>Tool defaults JSON<textarea value={editing.toolDefaultsJson} onChange={(event) => update("toolDefaultsJson", event.target.value)} /></label>
      <label>Contrast ×10<input type="number" min="45" value={editing.contrastRatioX10} onChange={(event) => update("contrastRatioX10", Number(event.target.value))} /></label>
      <label>Capability mask<input value={`0x${editing.capabilityMask.toString(16)}`} onChange={(event) => { if (/^0x[0-9a-f]+$/i.test(event.target.value)) update("capabilityMask", BigInt(event.target.value)); }} /></label>
      <label><input type="checkbox" checked={editing.classroomRestricted} onChange={(event) => update("classroomRestricted", event.target.checked)} /> Classroom restrictions</label>
      <label><input type="checkbox" checked={editing.notificationsEnabled} onChange={(event) => update("notificationsEnabled", event.target.checked)} /> Notifications</label>
      <label><input type="checkbox" checked={editing.hapticsEnabled} onChange={(event) => update("hapticsEnabled", event.target.checked)} /> Haptics</label>
    </div>
    <ul>{changes.length ? changes.map((change) => <li key={change}>{change}</li>) : <li>No changes</li>}</ul>
    <label>Portable profile<textarea value={portableProfile} onChange={(event) => setPortableProfile(event.target.value)} /></label>
    <div className="update-actions"><button type="button" onClick={() => setPortableProfile(exportProfile(editing))} disabled={!canApplyProfile(editing, roleCapabilityMask)}>Export</button><button type="button" onClick={importPortable} disabled={!portableProfile}>Import</button></div>
    <div className="update-actions"><button type="button" onClick={() => void preview()} disabled={busy || !canApplyProfile(editing, roleCapabilityMask)}>Preview on device</button><button type="button" onClick={() => void apply()} disabled={busy || !previewed}>Apply atomically</button></div>
    {message && <p role="status">{message}</p>}
  </section>;
}
