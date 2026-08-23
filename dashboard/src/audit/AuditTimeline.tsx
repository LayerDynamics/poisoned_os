import { useEffect, useState, type ReactElement } from "react";
import { AuditClient, type AuditEvent, type AuditSession, type AuditSnapshot } from "./AuditClient";

export function AuditTimeline({ events, verified }: { events: readonly AuditEvent[]; verified: boolean }): ReactElement {
  return <section aria-label="Audit timeline">
    <p>{verified ? "Full retained chain verified" : "Retained window verified; earlier events were truncated"}</p>
    <ol>{events.map((event) => <li key={event.eventId.toString()}>
      <strong>#{event.eventId.toString()} {event.action}</strong> · {event.resource} · {event.decision}
      <small> · {event.safeMetadata}</small>
    </li>)}</ol>
  </section>;
}

export function AuditTimelinePanel({ session }: { session: AuditSession }): ReactElement {
  const [snapshot, setSnapshot] = useState<AuditSnapshot | null>(null);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => {
    let active = true;
    let timer: ReturnType<typeof setTimeout> | null = null;
    const abort = new AbortController();
    const client = new AuditClient(session);
    const refresh = async () => {
      try {
        const next = await client.read(0n, 16, abort.signal);
        if (active) { setSnapshot(next); setError(null); }
      } catch (reason) {
        if (active) setError(reason instanceof Error ? reason.message : String(reason));
      } finally {
        if (active) timer = setTimeout(() => void refresh(), 5_000);
      }
    };
    void refresh();
    return () => {
      active = false;
      abort.abort();
      if (timer) clearTimeout(timer);
    };
  }, [session]);

  return <section className="update-card audit-panel" aria-labelledby="audit-title">
    <div className="update-heading">
      <div><p className="eyebrow">AUTHENTICATED DEVICE RECORD</p><h2 id="audit-title">Audit timeline</h2></div>
      <output className="update-state">{snapshot ? `${snapshot.events.length} retained events` : "loading"}</output>
    </div>
    {snapshot && <AuditTimeline events={snapshot.events} verified={snapshot.verified} />}
    {!snapshot && !error && <p role="status">Reading encrypted audit records…</p>}
    {error && <p className="error" role="alert">{error}</p>}
  </section>;
}
