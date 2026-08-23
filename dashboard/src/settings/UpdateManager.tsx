import { create, toBinary } from "@bufbuild/protobuf";
import { useRef, useState, type ReactElement } from "react";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import {
  ContentUpdateOperation,
  ContentUpdateRequestSchema,
  ContentUpdateState,
  ContentUpdateType,
  type ContentUpdateStatus,
} from "../generated/poison_packages_pb";
import { RebootRequestSchema, RebootRequest_RebootMode } from "../generated/system_pb";

export interface UpdateRequestClient {
  request(request: Main, signal?: AbortSignal): Promise<Main>;
  send(channel: string, payload: Uint8Array, signal?: AbortSignal): Promise<bigint>;
}

export interface UpdateDraft {
  manifestPath: string;
  candidateDigest: string;
}

export function createUpdateRequest(
  commandId: number,
  operation: ContentUpdateOperation,
  draft: UpdateDraft,
  currentStatus: ContentUpdateStatus | null = null,
  confirmationToken = new Uint8Array(),
): Main {
  return create(MainSchema, {
    commandId,
    content: {
      case: "poisonContentUpdateRequest",
      value: create(ContentUpdateRequestSchema, {
        operation,
        updateId: currentStatus?.updateId ?? "",
        contentType: currentStatus?.contentType ?? ContentUpdateType.FIRMWARE,
        manifestPath: draft.manifestPath,
        candidateDigest: draft.candidateDigest,
        receivedBytes: operation === ContentUpdateOperation.STAGE ? currentStatus?.contentBytes ?? 0 : 0,
        confirmationToken,
        healthy: false,
      }),
    },
  });
}

const STATE_NAMES: Record<ContentUpdateState, string> = {
  [ContentUpdateState.DISCOVERED]: "discovered",
  [ContentUpdateState.RECEIVING]: "receiving",
  [ContentUpdateState.STAGED]: "staged",
  [ContentUpdateState.VERIFIED]: "verified",
  [ContentUpdateState.AWAITING_CONFIRMATION]: "awaiting confirmation",
  [ContentUpdateState.ACTIVATING]: "activating",
  [ContentUpdateState.HEALTHY]: "healthy",
  [ContentUpdateState.ROLLED_BACK]: "rolled back",
  [ContentUpdateState.QUARANTINED]: "quarantined",
};

const EMPTY_DRAFT: UpdateDraft = {
  manifestPath: "/ext/update/poison/update.poison",
  candidateDigest: "",
};

export function UpdateManager({ session }: { session: UpdateRequestClient }): ReactElement {
  const [draft, setDraft] = useState<UpdateDraft>(EMPTY_DRAFT);
  const [status, setStatus] = useState<ContentUpdateStatus | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const commandId = useRef(1000);
  const confirmationToken = useRef(new Uint8Array());

  const setText = (field: keyof UpdateDraft, value: string) =>
    setDraft((current) => ({ ...current, [field]: value }));

  const run = async (operation: ContentUpdateOperation) => {
    setBusy(true);
    setError(null);
    try {
      const response = await session.request(createUpdateRequest(
        commandId.current++, operation, draft, status, confirmationToken.current,
      ));
      if (response.commandStatus !== CommandStatus.OK ||
          response.content.case !== "poisonContentUpdateStatus") {
        throw new Error(`device rejected update operation ${ContentUpdateOperation[operation]}`);
      }
      setStatus(response.content.value);
      confirmationToken.current = response.content.value.confirmationToken.slice();
      if (operation === ContentUpdateOperation.ACTIVATE) {
        const reboot = create(MainSchema, {
          commandId: commandId.current++,
          content: {
            case: "systemRebootRequest",
            value: create(RebootRequestSchema, { mode: RebootRequest_RebootMode.UPDATE }),
          },
        });
        await session.send("rpc", toBinary(MainSchema, reboot));
      }
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="update-card" aria-labelledby="update-title">
      <div className="update-heading">
        <div><p className="eyebrow">AUTHENTICATED CONTENT TRANSACTION</p><h2 id="update-title">Device update manager</h2></div>
        <output className="update-state">{status ? STATE_NAMES[status.state] : "not imported"}</output>
      </div>
      <div className="update-grid">
        <label>Signed archive path<input value={draft.manifestPath} onChange={(event) => setText("manifestPath", event.target.value)} /></label>
        <label>Archive SHA-256<input value={draft.candidateDigest} onChange={(event) => setText("candidateDigest", event.target.value)} /></label>
      </div>
      <div className="update-actions">
        <button disabled={busy} onClick={() => void run(ContentUpdateOperation.IMPORT)}>Import</button>
        <button disabled={busy || !status} onClick={() => void run(ContentUpdateOperation.STAGE)}>Stage</button>
        <button disabled={busy || !status} onClick={() => void run(ContentUpdateOperation.VERIFY)}>Verify</button>
        <button disabled={busy || confirmationToken.current.byteLength !== 16} onClick={() => void run(ContentUpdateOperation.ACTIVATE)}>Activate on device</button>
        <button disabled={busy || !status} onClick={() => void run(ContentUpdateOperation.HEALTH)}>Refresh boot health</button>
        <button className="secondary" disabled={busy || !status} onClick={() => void run(ContentUpdateOperation.INSPECT)}>Inspect</button>
        <button className="secondary" disabled={busy || !status} onClick={() => void run(ContentUpdateOperation.CANCEL)}>Cancel</button>
        <button className="secondary" disabled={busy || !status} onClick={() => void run(ContentUpdateOperation.ROLLBACK)}>Rollback</button>
        <button className="secondary" disabled={busy || !status} onClick={() => void run(ContentUpdateOperation.QUARANTINE)}>Quarantine</button>
      </div>
      {status && <p className="update-result" role="status">{status.updateId} · {ContentUpdateType[status.contentType]} · {status.result} · {status.receivedBytes}/{status.contentBytes} bytes · signed release {status.releaseSequence}</p>}
      {error && <p className="error" role="alert">{error}</p>}
    </section>
  );
}
