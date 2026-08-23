import { create } from "@bufbuild/protobuf";
import { useEffect, useRef, useState, type ReactElement } from "react";
import { CommandStatus, MainSchema } from "../generated/flipper_pb";
import { PingRequestSchema } from "../generated/system_pb";
import { SessionClient } from "../session/SessionClient";
import { UpdateManager } from "../settings/UpdateManager";
import { DeviceDiagnostics } from "../support/SupportBundle";
import { DeviceControlPanel } from "../device/DeviceControlPanel";
import { DeviceStatusPanel } from "../device/DeviceStatus";
import { selectWifiBoard, WebRuntimeTransport } from "../transports/WifiGatewayTransport";
import { PackageManager } from "../packages/PackageManager";
import { ProfileEditor, type ProfileDraft } from "../customization/ProfileEditor";
import type { DiscoveredDevice, Transport } from "../transports/Transport";
import { ToolWorkspace } from "../tools/ToolRunView";
import { PolicyClient } from "../session/PolicyClient";
import { Role } from "../generated/poison_policy_pb";
import { FileTransferPanel } from "../files/FileTransferPanel";
import { SafeSamplePanel } from "../apps/StructuredAppView";
import { AuditTimelinePanel } from "../audit/AuditTimeline";
import { JavaScriptWorkspace } from "../workloads/javascript/JavaScriptWorkspace";
import type { JavaScriptManifest } from "../workloads/javascript/manifest";

type ConnectionState = "disconnected" | "connecting" | "browser-approval" | "device-approval" | "verifying" | "connected" | "suspended" | "resuming";

interface PairingPrompt {
  confirmationCode: string;
  fingerprint: string;
}

const OPERATOR_CAPABILITIES = 0x3f;
const DIGEST = /^[0-9a-f]{64}$/;

function requestedWifiBoard(): string | null {
  const id = new URLSearchParams(window.location.search).get("wifiBoardId");
  if (id === null) return null;
  if (!/^[a-z][a-z0-9-]{0,62}$/.test(id)) {
    throw new Error("Wi-Fi board identity is invalid");
  }
  return id;
}

function servedUiFromLocation(): JavaScriptManifest["servedUi"] {
  const parameters = new URLSearchParams(window.location.search);
  const bundleId = parameters.get("servedBundleId");
  const version = parameters.get("servedBundleVersion");
  const contentSha256 = parameters.get("servedBundleSha256");
  if (bundleId === null && version === null && contentSha256 === null) return null;
  if (!bundleId || !/^[a-z0-9][a-z0-9._-]{0,63}$/.test(bundleId) ||
      !version || !/^\d+(?:\.\d+){0,2}$/.test(version) || !contentSha256 ||
      !DIGEST.test(contentSha256)) {
    throw new Error("served interface URL identity is invalid");
  }
  return { bundleId, version, contentSha256 };
}

const DEFAULT_PROFILE: ProfileDraft = {
  id: "poisonedos.field-operator",
  version: "1.0.0",
  role: "field",
  policyId: "builtin.field",
  enabledTools: ["nfc", "lf-rfid", "ibutton", "infrared", "subghz", "gpio", "usb-hid", "ble", "serial", "storage"],
  favorites: ["nfc", "subghz", "gpio"],
  hiddenTools: [],
  shortcuts: ["nfc", "subghz"],
  themeId: "builtin.field-console",
  fontPackId: "builtin.default",
  iconPackId: "builtin.default",
  menuId: "builtin.field-console",
  dashboardLayout: "field-console",
  homePresentation: "builtin.field-console",
  statusPresentation: "builtin.field-console",
  lockBehavior: "pin",
  notificationsEnabled: true,
  hapticsEnabled: true,
  toolDefaultsJson: "{}",
  transportPolicy: "local-only",
  loggingPolicy: "metadata",
  evidencePolicy: "digest-only",
  radioRegion: "device",
  peripheralSafety: "guarded",
  classroomPolicy: "none",
  contrastRatioX10: 45,
  capabilityMask: BigInt(OPERATOR_CAPABILITIES),
  classroomRestricted: false,
};

const DEFAULT_JAVASCRIPT_MANIFEST: JavaScriptManifest = {
  format: 1,
  id: "org.poisonedos.hello",
  name: "Poisoned_Os JavaScript",
  version: "1.0.0",
  language: "javascript",
  entrypoint: "src/main.js",
  runtime: "poison-mjs-1",
  runtimeApi: 1,
  firmwareApi: ">=1.0.0 <2.0.0",
  capabilities: ["runtime.event-loop"],
  limits: {
    heapBytes: 32_768,
    wallTimeMs: 5_000,
    logBytes: 16_384,
    artifactBytes: 131_072,
  },
  dependencies: "poison-js.lock",
  servedUi: servedUiFromLocation(),
};

const DEFAULT_JAVASCRIPT_FILES = {
  "src/main.js": "print(\"Poisoned_Os workload ready\");\n",
  "poison-js.lock": "{\n  \"schema\": \"poison.javascript.lock/v1\",\n  \"runtime\": \"poison-mjs-1\",\n  \"entrypoint\": \"src/main.js\",\n  \"dependencies\": []\n}\n",
} as const;

function fingerprint(digest: Uint8Array): string {
  return Array.from(digest.slice(0, 8), (value) => value.toString(16).padStart(2, "0")).join("");
}

export function App(): ReactElement {
  const [connection, setConnection] = useState<ConnectionState>("disconnected");
  const [error, setError] = useState<string | null>(null);
  const [prompt, setPrompt] = useState<PairingPrompt | null>(null);
  const sessionRef = useRef<SessionClient | null>(null);
  const approvalRef = useRef<((approved: boolean) => void) | null>(null);

  useEffect(() => {
    return () => {
      approvalRef.current?.(false);
      void sessionRef.current?.close();
    };
  }, []);

  const connectSession = async (
    transport: Transport,
    device: DiscoveredDevice,
  ) => {
    setError(null);
    setConnection("connecting");
    const session = new SessionClient();
    sessionRef.current = session;
    const removeStatusHandler = session.onStatus((status) => {
      if (status === "suspended" && sessionRef.current === session) {
        setConnection("suspended");
      } else if (status === "resuming" && sessionRef.current === session) {
        setConnection("resuming");
      } else if (status === "active" && sessionRef.current === session) {
        setConnection("connected");
      }
      if (status === "disconnected" && sessionRef.current === session) {
        sessionRef.current = null;
        setPrompt(null);
        setConnection("disconnected");
      }
    });
    try {
      await session.connect(
        transport,
        device,
        {
          clientName: "Poisoned_Os Dashboard",
          requestedRole: 1,
          requestedCapabilities: OPERATOR_CAPABILITIES,
          approve: ({ confirmationCode, transcriptDigest }) => new Promise<boolean>((resolve) => {
            approvalRef.current = resolve;
            setPrompt({ confirmationCode, fingerprint: fingerprint(transcriptDigest) });
            setConnection("browser-approval");
          }),
        },
      );
      setConnection("verifying");
      const challenge = crypto.getRandomValues(new Uint8Array(16));
      const response = await session.request(create(MainSchema, {
        commandId: 1,
        content: {
          case: "systemPingRequest",
          value: create(PingRequestSchema, { data: challenge }),
        },
      }));
      if (response.commandStatus !== CommandStatus.OK || response.content.case !== "systemPingResponse" ||
          response.content.value.data.byteLength !== challenge.byteLength ||
          response.content.value.data.some((value, index) => value !== challenge[index])) {
        throw new Error("encrypted device verification failed");
      }
      const policy = await new PolicyClient(session).evaluate(
        Role.OPERATOR,
        OPERATOR_CAPABILITIES,
        false,
        true,
      );
      if (!policy.allowed || policy.grantedCapabilities !== OPERATOR_CAPABILITIES) {
        throw new Error(policy.denialReason || "device rejected the negotiated operator policy");
      }
      setConnection("connected");
    } catch (reason) {
      await session.close();
      sessionRef.current = null;
      setConnection("disconnected");
      setError(reason instanceof Error ? reason.message : String(reason));
    } finally {
      if (session.status === "disconnected") removeStatusHandler();
    }
  };

  const connectWifi = async () => {
    setError(null);
    setConnection("connecting");
    try {
      if (!window.isSecureContext) {
        throw new Error("Open the dashboard over HTTPS when connecting from another device");
      }
      const transport = new WebRuntimeTransport();
      const devices = await transport.discover();
      const device = selectWifiBoard(devices, requestedWifiBoard());
      if (!device) throw new Error("no configured Wi-Fi board is available");
      await connectSession(transport, device);
    } catch (reason) {
      setConnection("disconnected");
      setError(reason instanceof Error ? reason.message : String(reason));
    }
  };

  const answerPairing = (approved: boolean) => {
    const resolve = approvalRef.current;
    approvalRef.current = null;
    setPrompt(null);
    if (approved) setConnection("device-approval");
    resolve?.(approved);
  };

  const resumeSession = async () => {
    const session = sessionRef.current;
    if (!session) return;
    setError(null);
    try {
      const transport = new WebRuntimeTransport();
      const device = selectWifiBoard(await transport.discover(), requestedWifiBoard());
      if (!device) throw new Error("no configured Wi-Fi board is available");
      await session.resume(transport, device);
    } catch (reason) {
      sessionRef.current = null;
      setConnection("disconnected");
      setError(reason instanceof Error ? reason.message : String(reason));
    }
  };

  const disconnect = async () => {
    approvalRef.current?.(false);
    approvalRef.current = null;
    await sessionRef.current?.disconnect();
    sessionRef.current = null;
    setPrompt(null);
    setConnection("disconnected");
  };

  return (
    <main className="app-shell">
      <header>
        <p className="eyebrow">POISONED_OS / WI-FI CONTROL</p>
        <h1>Control your Flipper without taking it out.</h1>
        <p>The local Node.js runtime carries the full interface over Wi-Fi while the device stays where it is.</p>
      </header>

      <section className="connection-card" aria-labelledby="connection-title">
        <div>
          <p className="eyebrow" id="connection-title">FLIPPER ZERO · WEB OVER WI-FI</p>
          <p className="connection-state" role="status">{connection.replaceAll("-", " ")}</p>
          <p className="transport-id">HTTP(S) dashboard · WS(S) RPC · local Node.js runtime · Wi-Fi board</p>
        </div>
        {connection === "disconnected" ? <div className="actions">
          <button type="button" onClick={() => void connectWifi()}>Connect over Wi-Fi</button>
        </div> : connection === "suspended" ? <div className="actions">
          <button type="button" onClick={() => void resumeSession()}>Reconnect</button>
          <button type="button" className="secondary" onClick={() => void disconnect()}>Forget session</button>
        </div> :
          <button type="button" className="secondary" onClick={() => void disconnect()}>Disconnect</button>}
      </section>

      {prompt && (
        <dialog open className="pairing-dialog" aria-labelledby="pairing-title">
          <p className="eyebrow">PAIRING CHALLENGE</p>
          <h2 id="pairing-title">Match this code on the device</h2>
          <output className="pairing-code">{prompt.confirmationCode}</output>
          <p className="fingerprint">Fingerprint {prompt.fingerprint}</p>
          <p>Continue here, then press Approve on the Flipper itself. Both confirmations are required.</p>
          <div className="actions">
            <button type="button" onClick={() => answerPairing(true)}>Code matches</button>
            <button type="button" className="secondary" onClick={() => answerPairing(false)}>Cancel</button>
          </div>
        </dialog>
      )}

      {connection === "device-approval" && <p className="notice">Approve the matching code on the Flipper.</p>}
      {connection === "connected" && <p className="success">Encrypted RPC ping verified. Device control is active.</p>}
      {connection === "connected" && sessionRef.current && <DeviceStatusPanel session={sessionRef.current} />}
      {connection === "connected" && sessionRef.current && <DeviceControlPanel session={sessionRef.current} />}
      {connection === "connected" && sessionRef.current && <SafeSamplePanel session={sessionRef.current} />}
      {connection === "connected" && sessionRef.current && <AuditTimelinePanel session={sessionRef.current} />}
      {connection === "connected" && sessionRef.current && <UpdateManager session={sessionRef.current} />}
      {connection === "connected" && sessionRef.current && <PackageManager session={sessionRef.current} />}
      {connection === "connected" && sessionRef.current && <ProfileEditor draft={DEFAULT_PROFILE} roleCapabilityMask={BigInt(OPERATOR_CAPABILITIES)} session={sessionRef.current} />}
      {connection === "connected" && sessionRef.current && <ToolWorkspace session={sessionRef.current} />}
      {connection === "connected" && sessionRef.current && <FileTransferPanel session={sessionRef.current} />}
      {connection === "connected" && sessionRef.current && <JavaScriptWorkspace session={sessionRef.current} manifest={DEFAULT_JAVASCRIPT_MANIFEST} initialFiles={DEFAULT_JAVASCRIPT_FILES} />}
      {connection === "connected" && sessionRef.current && <DeviceDiagnostics session={sessionRef.current} />}
      {error && <p className="error" role="alert">{error}</p>}
      <footer>Node.js runtime · Wi-Fi board · Poisoned_Os device session</footer>
    </main>
  );
}
