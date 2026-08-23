import { useState, type FormEvent, type ReactElement } from "react";

export type LfRfidOperation = "read" | "write" | "emulate";

export interface LfRfidCapabilities {
  readonly read: boolean;
  readonly write: boolean;
  readonly emulate: boolean;
}

export interface LfRfidRequest {
  readonly operation: LfRfidOperation;
  readonly timeoutMs: number;
}

export function canExecuteLfRfidOperation(
  operation: LfRfidOperation,
  capabilities: LfRfidCapabilities,
  exactConfirmation: boolean,
): boolean {
  if (operation === "read") return capabilities.read;
  if (operation === "write") return capabilities.write && exactConfirmation;
  return capabilities.emulate && exactConfirmation;
}

export function validateLfRfidRequest(request: LfRfidRequest): readonly string[] {
  if (!Number.isInteger(request.timeoutMs) || request.timeoutMs < 1 || request.timeoutMs > 60_000) {
    return ["Timeout must be between 1 and 60000 milliseconds."];
  }
  return [];
}

export function LfRfidTool({
  capabilities,
  active,
  busy,
  onRun,
  onStop,
}: {
  readonly capabilities: LfRfidCapabilities;
  readonly active: boolean;
  readonly busy: boolean;
  readonly onRun: (request: LfRfidRequest) => void | Promise<void>;
  readonly onStop: () => void | Promise<void>;
}): ReactElement {
  const [operation, setOperation] = useState<LfRfidOperation>("read");
  const [timeoutMs, setTimeoutMs] = useState(5000);
  const [exactConfirmation, setExactConfirmation] = useState(false);
  const request: LfRfidRequest = { operation, timeoutMs };
  const errors = validateLfRfidRequest(request);
  const permitted = canExecuteLfRfidOperation(operation, capabilities, exactConfirmation);

  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (!busy && errors.length === 0 && permitted) void onRun(request);
  };

  return <form aria-label="LF RFID tool" onSubmit={submit}>
    <h3>LF RFID</h3>
    <p>Credential bytes are retained only in evidence storage; browser results expose protocol, size, and SHA-256.</p>
    <label>Operation
      <select value={operation} disabled={busy || active} onChange={(event) => setOperation(event.currentTarget.value as LfRfidOperation)}>
        <option value="read">Read and inspect</option>
        <option value="write">Write</option>
        <option value="emulate">Emulate</option>
      </select>
    </label>
    <label>Timeout (milliseconds)
      <input type="number" min={1} max={60_000} value={timeoutMs} disabled={busy || active} onChange={(event) => setTimeoutMs(event.currentTarget.valueAsNumber)} />
    </label>
    <label>
      <input type="checkbox" checked={exactConfirmation} disabled={busy || active || operation === "read"} onChange={(event) => setExactConfirmation(event.currentTarget.checked)} />
      Exact target confirmation
    </label>
    {errors.map((error) => <p className="error" role="alert" key={error}>{error}</p>)}
    {!permitted && <p className="error" role="alert">This operation is not permitted by the current device capability and confirmation state.</p>}
    <div className="actions">
      <button type="submit" disabled={busy || active || errors.length > 0 || !permitted}>Run LF RFID operation</button>
      <button type="button" className="secondary" disabled={busy || !active} onClick={() => void onStop()}>Stop and release LF RFID hardware</button>
    </div>
  </form>;
}
