import { useState, type FormEvent, type ReactElement } from "react";

export type IButtonOperation = "read" | "write" | "emulate";

export interface IButtonCapabilities {
  readonly read: boolean;
  readonly write: boolean;
  readonly emulate: boolean;
}

export interface IButtonRequest {
  readonly operation: IButtonOperation;
  readonly timeoutMs: number;
}

export function canExecuteIButtonOperation(
  operation: IButtonOperation,
  capabilities: IButtonCapabilities,
  exactConfirmation: boolean,
): boolean {
  if (operation === "read") return capabilities.read;
  if (operation === "write") return capabilities.write && exactConfirmation;
  return capabilities.emulate && exactConfirmation;
}

export function validateIButtonRequest(request: IButtonRequest): readonly string[] {
  if (!Number.isInteger(request.timeoutMs) || request.timeoutMs < 1 || request.timeoutMs > 60_000) {
    return ["Timeout must be between 1 and 60000 milliseconds."];
  }
  return [];
}

export function IButtonTool({
  capabilities,
  active,
  busy,
  onRun,
  onStop,
}: {
  readonly capabilities: IButtonCapabilities;
  readonly active: boolean;
  readonly busy: boolean;
  readonly onRun: (request: IButtonRequest) => void | Promise<void>;
  readonly onStop: () => void | Promise<void>;
}): ReactElement {
  const [operation, setOperation] = useState<IButtonOperation>("read");
  const [timeoutMs, setTimeoutMs] = useState(5000);
  const [exactConfirmation, setExactConfirmation] = useState(false);
  const request: IButtonRequest = { operation, timeoutMs };
  const errors = validateIButtonRequest(request);
  const permitted = canExecuteIButtonOperation(operation, capabilities, exactConfirmation);

  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (!busy && errors.length === 0 && permitted) void onRun(request);
  };

  return <form aria-label="iButton and 1-Wire tool" onSubmit={submit}>
    <h3>iButton / 1-Wire</h3>
    <p>Reading and mutation may power the 1-Wire bus. Key bytes remain in evidence storage; browser output contains only protocol, validation, size, and SHA-256.</p>
    <label>Operation
      <select value={operation} disabled={busy || active} onChange={(event) => setOperation(event.currentTarget.value as IButtonOperation)}>
        <option value="read">Read and validate</option>
        <option value="write">Write identifier</option>
        <option value="emulate">Emulate</option>
      </select>
    </label>
    <label>Timeout (milliseconds)
      <input type="number" min={1} max={60_000} value={timeoutMs} disabled={busy || active} onChange={(event) => setTimeoutMs(event.currentTarget.valueAsNumber)} />
    </label>
    <label>
      <input type="checkbox" checked={exactConfirmation} disabled={busy || active || operation === "read"} onChange={(event) => setExactConfirmation(event.currentTarget.checked)} />
      Exact target and bus-power confirmation
    </label>
    {errors.map((error) => <p className="error" role="alert" key={error}>{error}</p>)}
    {!permitted && <p className="error" role="alert">This operation is not permitted by the current device capability and confirmation state.</p>}
    <div className="actions">
      <button type="submit" disabled={busy || active || errors.length > 0 || !permitted}>Run iButton operation</button>
      <button type="button" className="secondary" disabled={busy || !active} onClick={() => void onStop()}>Stop and release 1-Wire hardware</button>
    </div>
  </form>;
}
