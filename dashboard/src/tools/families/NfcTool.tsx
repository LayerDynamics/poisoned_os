import { useState, type FormEvent, type ReactElement } from "react";

export type NfcOperation = "detect" | "raw-capture" | "write" | "emulate";

export interface NfcCapabilities {
  readonly read: boolean;
  readonly rawCapture: boolean;
  readonly write: boolean;
  readonly emulate: boolean;
}

export interface NfcRequest {
  readonly operation: NfcOperation;
  readonly timeoutMs: number;
  readonly maximumCaptureBytes: number;
}

export function canExecuteNfcOperation(
  operation: NfcOperation,
  capabilities: NfcCapabilities,
  exactConfirmation: boolean,
): boolean {
  if (operation === "detect") return capabilities.read;
  if (operation === "raw-capture") return capabilities.rawCapture;
  if (operation === "write") return capabilities.write && exactConfirmation;
  return capabilities.emulate && exactConfirmation;
}

export function validateNfcRequest(request: NfcRequest): readonly string[] {
  const errors: string[] = [];
  if (!Number.isInteger(request.timeoutMs) || request.timeoutMs < 1 || request.timeoutMs > 60_000) {
    errors.push("Timeout must be between 1 and 60000 milliseconds.");
  }
  if (request.operation === "detect") {
    if (request.maximumCaptureBytes !== 0) errors.push("Detection does not accept capture bytes.");
  } else if (
    !Number.isInteger(request.maximumCaptureBytes)
    || request.maximumCaptureBytes < 1
    || request.maximumCaptureBytes > 8192
  ) {
    errors.push("Raw capture is limited to 8192 bytes.");
  }
  return errors;
}

export function NfcTool({
  capabilities,
  active,
  busy,
  onRun,
  onStop,
}: {
  readonly capabilities: NfcCapabilities;
  readonly active: boolean;
  readonly busy: boolean;
  readonly onRun: (request: NfcRequest) => void | Promise<void>;
  readonly onStop: () => void | Promise<void>;
}): ReactElement {
  const [operation, setOperation] = useState<NfcOperation>("detect");
  const [timeoutMs, setTimeoutMs] = useState(5000);
  const [maximumCaptureBytes, setMaximumCaptureBytes] = useState(1024);
  const [exactConfirmation, setExactConfirmation] = useState(false);
  const request: NfcRequest = {
    operation,
    timeoutMs,
    maximumCaptureBytes: operation === "detect" ? 0 : maximumCaptureBytes,
  };
  const errors = validateNfcRequest(request);
  const permitted = canExecuteNfcOperation(operation, capabilities, exactConfirmation);

  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (busy || errors.length > 0 || !permitted) return;
    void onRun(request);
  };

  return <form aria-label="NFC tool" onSubmit={submit}>
    <h3>NFC</h3>
    <p>Inspect a tag, capture bounded raw data, or perform an explicitly approved mutation.</p>
    <label>Operation
      <select value={operation} disabled={busy || active} onChange={(event) => setOperation(event.target.value as NfcOperation)}>
        <option value="detect">Detect and inspect</option>
        <option value="raw-capture">Raw capture</option>
        <option value="write">Write</option>
        <option value="emulate">Emulate</option>
      </select>
    </label>
    <label>Timeout (milliseconds)
      <input type="number" min={1} max={60_000} value={timeoutMs} disabled={busy || active} onChange={(event) => setTimeoutMs(event.currentTarget.valueAsNumber)} />
    </label>
    {operation !== "detect" && <label>Maximum capture bytes
      <input type="number" min={1} max={8192} value={maximumCaptureBytes} disabled={busy || active} onChange={(event) => setMaximumCaptureBytes(event.currentTarget.valueAsNumber)} />
    </label>}
    <label>
      <input type="checkbox" checked={exactConfirmation} disabled={busy || active || (operation !== "write" && operation !== "emulate")} onChange={(event) => setExactConfirmation(event.currentTarget.checked)} />
      Exact target confirmation
    </label>
    {errors.map((error) => <p className="error" role="alert" key={error}>{error}</p>)}
    {!permitted && <p className="error" role="alert">This operation is not permitted by the current device capability and confirmation state.</p>}
    <div className="actions">
      <button type="submit" disabled={busy || active || errors.length > 0 || !permitted}>Run NFC operation</button>
      <button type="button" className="secondary" disabled={busy || !active} onClick={() => void onStop()}>Stop and release NFC hardware</button>
    </div>
  </form>;
}
