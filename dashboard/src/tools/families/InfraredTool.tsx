import { useState, type FormEvent, type ReactElement } from "react";

export type InfraredOperation = "receive" | "transmit";

export interface InfraredCapabilities {
  readonly receive: boolean;
  readonly transmit: boolean;
}

export interface InfraredRequest {
  readonly operation: InfraredOperation;
  readonly timeoutMs: number;
  readonly maximumTimings: number;
  readonly exactConfirmation: boolean;
}

export function canExecuteInfraredOperation(
  operation: InfraredOperation,
  capabilities: InfraredCapabilities,
  exactConfirmation: boolean,
): boolean {
  return operation === "receive"
    ? capabilities.receive
    : capabilities.transmit && exactConfirmation;
}

export function validateInfraredRequest(request: InfraredRequest): readonly string[] {
  const errors: string[] = [];
  if (!Number.isInteger(request.timeoutMs) || request.timeoutMs < 1 || request.timeoutMs > 60_000) {
    errors.push("Timeout must be between 1 and 60000 milliseconds.");
  }
  if (!Number.isInteger(request.maximumTimings) || request.maximumTimings < 1 || request.maximumTimings > 1024) {
    errors.push("Raw captures are limited to 1024 timings.");
  }
  return errors;
}

export function InfraredTool({
  capabilities,
  active,
  busy,
  onRun,
  onStop,
}: {
  readonly capabilities: InfraredCapabilities;
  readonly active: boolean;
  readonly busy: boolean;
  readonly onRun: (request: InfraredRequest) => void | Promise<void>;
  readonly onStop: () => void | Promise<void>;
}): ReactElement {
  const [operation, setOperation] = useState<InfraredOperation>("receive");
  const [timeoutMs, setTimeoutMs] = useState(5000);
  const [maximumTimings, setMaximumTimings] = useState(1024);
  const [exactConfirmation, setExactConfirmation] = useState(false);
  const request: InfraredRequest = { operation, timeoutMs, maximumTimings, exactConfirmation };
  const errors = validateInfraredRequest(request);
  const permitted = canExecuteInfraredOperation(operation, capabilities, exactConfirmation);

  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (!busy && errors.length === 0 && permitted) void onRun(request);
  };

  return <form aria-label="Infrared tool" onSubmit={submit}>
    <h3>Infrared</h3>
    <p>Complete raw timing evidence is preserved separately; decoded browser output includes protocol, address, command, repeat state, and digest.</p>
    <label>Operation
      <select value={operation} disabled={busy} onChange={(event) => setOperation(event.currentTarget.value as InfraredOperation)}>
        <option value="receive">Receive and inspect</option>
        <option value="transmit">Transmit / replay</option>
      </select>
    </label>
    <label>Timeout (milliseconds)
      <input type="number" min={1} max={60_000} value={timeoutMs} disabled={busy || active} onChange={(event) => setTimeoutMs(event.currentTarget.valueAsNumber)} />
    </label>
    <label>Maximum raw timings
      <input type="number" min={1} max={1024} value={maximumTimings} disabled={busy || active} onChange={(event) => setMaximumTimings(event.currentTarget.valueAsNumber)} />
    </label>
    <label>
      <input type="checkbox" checked={exactConfirmation} disabled={busy || operation === "receive"} onChange={(event) => setExactConfirmation(event.currentTarget.checked)} />
      Exact signal and target confirmation
    </label>
    {errors.map((error) => <p className="error" role="alert" key={error}>{error}</p>)}
    {!permitted && <p className="error" role="alert">This operation is not permitted by the current device capability and confirmation state.</p>}
    <div className="actions">
      <button type="submit" disabled={busy || errors.length > 0 || !permitted}>Run infrared operation</button>
      <button type="button" className="secondary" disabled={busy || !active} onClick={() => void onStop()}>Stop and release infrared hardware</button>
    </div>
  </form>;
}
