import { useEffect, useMemo, useState, type ReactElement } from "react";
import type { AppEvent } from "./AppEvents";
import { AppArtifactList } from "./AppArtifactList";
import { AppConsole } from "./AppConsole";
import { StructuredAppClient, type StructuredAppSession } from "./StructuredAppClient";

export function StructuredAppView({ events, onCancel }: { events: readonly AppEvent[]; onCancel?: () => void }): ReactElement {
  return <section aria-label="Structured application">
    <h2>Structured application</h2>
    <ol aria-live="polite">{events.map((event) => <li key={`${event.runId}:${event.sequence.toString()}`}>
      <span>{event.kind}: {event.message}</span>
      {event.progressPercent !== undefined && <progress max={100} value={event.progressPercent}>{event.progressPercent}%</progress>}
      {event.schemaJson && <pre aria-label={`${event.kind} schema`}>{event.schemaJson}</pre>}
      {event.rowsJson && <pre aria-label="Table rows">{event.rowsJson}</pre>}
      {event.artifactIds.length > 0 && <small> · {event.artifactIds.length} artifact(s)</small>}
    </li>)}</ol>
    <button type="button" onClick={onCancel} disabled={!onCancel}>Cancel</button>
  </section>;
}

const SAFE_SAMPLE_APP_NAME = "Poison Safe Sample";
const SAFE_SAMPLE_APP_ID = "poison_safe_sample";
const SAFE_SAMPLE_RUN_ID = "onboarding";

function wait(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

export function SafeSamplePanel({ session }: { session: StructuredAppSession }): ReactElement {
  const [events, setEvents] = useState<readonly AppEvent[]>([]);
  const [parameter, setParameter] = useState(1);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState("ready to launch verified device sample");
  const client = useMemo(() => new StructuredAppClient(session, Date.now, (run) => setEvents(run.events)), [session]);
  useEffect(() => () => client.dispose(), [client]);

  const run = async () => {
    setBusy(true);
    try {
      if (!client.current) {
        setStatus("launching Poison Safe Sample on device");
        await client.launchApplication(SAFE_SAMPLE_APP_NAME);
        client.start(SAFE_SAMPLE_RUN_ID, parameter);
        let ready = false;
        for (let attempt = 0; attempt < 20 && !ready; attempt += 1) {
          try {
            await client.command(SAFE_SAMPLE_APP_ID, "status", "");
            ready = true;
          } catch (error) {
            if (attempt === 19) throw error;
            await wait(50);
          }
        }
      }
      setStatus(`running bounded parameter ${parameter}`);
      await client.command(SAFE_SAMPLE_APP_ID, "run", String(parameter));
      setStatus("device sample completed");
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error));
    } finally {
      setBusy(false);
    }
  };

  const cancel = async () => {
    setBusy(true);
    try {
      await client.command(SAFE_SAMPLE_APP_ID, "cancel", "", true);
      setStatus("device sample cancelled and closed");
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error));
    } finally {
      setBusy(false);
    }
  };
  const artifacts = events.flatMap((event) => event.artifactIds);

  return <section className="update-card safe-sample" aria-labelledby="safe-sample-title">
    <div className="update-heading">
      <div><p className="eyebrow">FIRMWARE-TO-DASHBOARD CONTRACT</p><h2 id="safe-sample-title">Poison Safe Sample</h2></div>
      <output className="update-state" aria-live="polite">{status}</output>
    </div>
    <label>Bounded parameter
      <select value={parameter} disabled={busy || client.current !== null} onChange={(event) => setParameter(Number(event.target.value))}>
        <option value={1}>1</option><option value={2}>2</option><option value={3}>3</option>
      </select>
    </label>
    <div className="actions">
      <button type="button" disabled={busy || client.current?.cancelled === true} onClick={() => void run()}>{client.current ? "Run again" : "Launch and run"}</button>
      <button type="button" className="secondary" disabled={busy || !client.current || client.current.cancelled} onClick={() => void cancel()}>Cancel and close</button>
    </div>
    <StructuredAppView events={events} onCancel={!busy && client.current && !client.current.cancelled ? () => void cancel() : undefined} />
    <AppConsole events={events} />
    <AppArtifactList artifactIds={artifacts} />
  </section>;
}
