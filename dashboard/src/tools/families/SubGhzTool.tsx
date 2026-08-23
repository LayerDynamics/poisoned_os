import { useState, type FormEvent, type ReactElement } from "react";

export type SubGhzOperation = "receive" | "analyze" | "transmit";

export interface SubGhzCapabilities {
  readonly receive: boolean;
  readonly analyze: boolean;
  readonly transmit: boolean;
}

export interface SubGhzRequest {
  readonly operation: SubGhzOperation;
  readonly frequencyHz: number;
  readonly timeoutMs: number;
  readonly maximumTimings: number;
  readonly exactConfirmation: boolean;
}

export function canExecuteSubGhzOperation(
  operation: SubGhzOperation,
  capabilities: SubGhzCapabilities,
  exactConfirmation: boolean,
): boolean {
  if (operation === "receive") return capabilities.receive;
  if (operation === "analyze") return capabilities.analyze;
  return capabilities.transmit && exactConfirmation;
}

export function validateSubGhzRequest(request: SubGhzRequest): readonly string[] {
  const errors: string[] = [];
  if (!Number.isInteger(request.frequencyHz) || request.frequencyHz < 1 || request.frequencyHz > 1_000_000_000) {
    errors.push("Frequency must be between 1 Hz and 1 GHz; firmware applies hardware and live region policy.");
  }
  if (!Number.isInteger(request.timeoutMs) || request.timeoutMs < 1 || request.timeoutMs > 60_000) {
    errors.push("Timeout must be between 1 and 60000 milliseconds.");
  }
  if (!Number.isInteger(request.maximumTimings) || request.maximumTimings < 1 || request.maximumTimings > 1024) {
    errors.push("Raw captures are limited to 1024 timings.");
  }
  return errors;
}

export function SubGhzTool({
  capabilities,
  active,
  busy,
  onRun,
  onStop,
}: {
  readonly capabilities: SubGhzCapabilities;
  readonly active: boolean;
  readonly busy: boolean;
  readonly onRun: (request: SubGhzRequest) => void | Promise<void>;
  readonly onStop: () => void | Promise<void>;
}): ReactElement {
  const [operation, setOperation] = useState<SubGhzOperation>("receive");
  const [frequencyHz, setFrequencyHz] = useState(433_920_000);
  const [timeoutMs, setTimeoutMs] = useState(5000);
  const [maximumTimings, setMaximumTimings] = useState(1024);
  const [exactConfirmation, setExactConfirmation] = useState(false);
  const request: SubGhzRequest = {
    operation,
    frequencyHz,
    timeoutMs,
    maximumTimings,
    exactConfirmation,
  };
  const errors = validateSubGhzRequest(request);
  const permitted = canExecuteSubGhzOperation(operation, capabilities, exactConfirmation);

  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (!busy && errors.length === 0 && permitted) void onRun(request);
  };

  return <form aria-label="Sub-GHz tool" onSubmit={submit}>
    <h3>Sub-GHz</h3>
    <p>Every analyzer or transmit action is rechecked against the radio hardware, live device region and profile policy, classroom mode, role, and capability.</p>
    <p>Raw timings are preserved as evidence separately from redacted derived decoder output.</p>
    <label>Operation
      <select value={operation} disabled={busy} onChange={(event) => setOperation(event.currentTarget.value as SubGhzOperation)}>
        <option value="receive">Receive and decode</option>
        <option value="analyze">Analyze raw signal</option>
        <option value="transmit">Replay current exact capture</option>
      </select>
    </label>
    <label>Frequency (Hz)
      <input type="number" min={1} max={1_000_000_000} value={frequencyHz} disabled={busy || active || operation === "transmit"} onChange={(event) => setFrequencyHz(event.currentTarget.valueAsNumber)} />
    </label>
    <label>Timeout (milliseconds)
      <input type="number" min={1} max={60_000} value={timeoutMs} disabled={busy || active || operation === "transmit"} onChange={(event) => setTimeoutMs(event.currentTarget.valueAsNumber)} />
    </label>
    <label>Maximum raw timings
      <input type="number" min={1} max={1024} value={maximumTimings} disabled={busy || active || operation === "transmit"} onChange={(event) => setMaximumTimings(event.currentTarget.valueAsNumber)} />
    </label>
    <label>
      <input type="checkbox" checked={exactConfirmation} disabled={busy || operation !== "transmit"} onChange={(event) => setExactConfirmation(event.currentTarget.checked)} />
      Exact signal, frequency, and target confirmation
    </label>
    {errors.map((error) => <p className="error" role="alert" key={error}>{error}</p>)}
    {!permitted && <p className="error" role="alert">This operation is not permitted by the current device capability and confirmation state.</p>}
    <div className="actions">
      <button type="submit" disabled={busy || errors.length > 0 || !permitted}>Run Sub-GHz operation</button>
      <button type="button" className="secondary" disabled={busy || !active} onClick={() => void onStop()}>Stop and release Sub-GHz radio</button>
    </div>
  </form>;
}
