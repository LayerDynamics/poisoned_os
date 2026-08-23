import { useEffect, useMemo, useState, type ReactElement } from "react";
import type { StructuredAppSession } from "../apps/StructuredAppClient";
import { ToolCatalog, type ToolCatalogEntry } from "./ToolCatalog";
import { ToolClient } from "./ToolClient";

export interface ToolRunEvent { sequence: bigint; kind: "log" | "progress" | "result" | "artifact"; message: string; }
export function appendToolRunEvent(events: readonly ToolRunEvent[], event: ToolRunEvent): readonly ToolRunEvent[] {
  const previous = events.at(-1);
  if (event.sequence < 0n || (previous && event.sequence !== previous.sequence + 1n) || event.message.length > 1024) throw new Error("invalid tool event");
  return [...events, event];
}
export function ToolRunView({ events, onStop }: { events: readonly ToolRunEvent[]; onStop?: () => void }): ReactElement { return <section aria-label="Tool run"><ol aria-live="polite">{events.map((event) => <li key={event.sequence.toString()}>{event.kind}: {event.message}</li>)}</ol><button type="button" onClick={onStop} disabled={!onStop}>Stop</button></section>; }

interface RunnableTool extends ToolCatalogEntry {
  readonly commandId: string;
  readonly defaultPayload: Readonly<Record<string, unknown>>;
}

const RUNNABLE_TOOLS: readonly RunnableTool[] = [
  { id: "nfc.read", family: "nfc", status: "foundation", purpose: "Detect supported NFC protocols without mutating the tag.", capabilities: ["nfc.read"], sample: "Present a tag for five seconds.", commandId: "nfc.detect", defaultPayload: { timeout_ms: 5000 } },
  { id: "lf-rfid.read", family: "lf-rfid", status: "foundation", purpose: "Detect and read a supported LF RFID credential.", capabilities: ["lf-rfid.read"], sample: "Present a credential for five seconds.", commandId: "lf-rfid.detect", defaultPayload: { timeout_ms: 5000 } },
  { id: "ibutton.read", family: "ibutton", status: "foundation", purpose: "Read an iButton or 1-Wire identifier.", capabilities: ["ibutton.read"], sample: "Touch a key to the contacts.", commandId: "ibutton.read", defaultPayload: {} },
  { id: "infrared.receive", family: "infrared", status: "foundation", purpose: "Receive bounded infrared signal metadata.", capabilities: ["infrared.receive"], sample: "Point a remote and press a button.", commandId: "infrared.receive", defaultPayload: { timeout_ms: 5000 } },
  { id: "sub-ghz.receive", family: "sub-ghz", status: "foundation", purpose: "Receive a region-valid Sub-GHz signal.", capabilities: ["sub-ghz.receive"], sample: "Listen on a permitted fixture frequency.", commandId: "sub-ghz.receive", defaultPayload: { frequency_hz: 433920000, timeout_ms: 5000 } },
  { id: "gpio.inspect", family: "gpio", status: "foundation", purpose: "Read one safe expansion GPIO pin.", capabilities: ["gpio.read"], sample: "Read PC0 as a high-impedance input.", commandId: "gpio.read", defaultPayload: { pin: "PC0" } },
  { id: "usb-hid.inspect", family: "usb-hid", status: "foundation", purpose: "Inspect USB HID connection state without sending input.", capabilities: ["usb-hid.inspect"], sample: "Read connection state.", commandId: "usb-hid.status", defaultPayload: {} },
  { id: "ble-hid.status", family: "ble-hid", status: "foundation", purpose: "Inspect BLE HID service state without advertising or input.", capabilities: ["ble.status"], sample: "Read active state.", commandId: "ble-hid.status", defaultPayload: {} },
  { id: "serial.observe", family: "serial", status: "foundation", purpose: "Observe bounded UART input with overflow reporting.", capabilities: ["serial.observe"], sample: "Capture up to 512 bytes from USART.", commandId: "serial.observe", defaultPayload: { port: "usart", baudrate: 115200, timeout_ms: 1000, capacity: 512 } },
  { id: "storage.inspect", family: "storage", status: "foundation", purpose: "Read or SHA-256 hash a logical VFS path.", capabilities: ["storage.read"], sample: "Hash a selected export.", commandId: "storage.inspect", defaultPayload: { operation: "sha256", path: "/exports/example.bin" } },
  { id: "marauder.console", family: "serial", status: "foundation", purpose: "Run a command from the bounded Marauder registry over an owned UART session.", capabilities: ["marauder.observe", "marauder.control", "marauder.capture", "marauder.active", "marauder.admin"], sample: "Request board information from the attached ESP32.", commandId: "marauder.command", defaultPayload: { command: "info", argument: "", port: "usart", baudrate: 115200, timeout_ms: 1000, capacity: 512 } },
];

export function ToolWorkspace({ session }: { session: StructuredAppSession }): ReactElement {
  const client = useMemo(() => new ToolClient(session), [session]);
  const [selected, setSelected] = useState<RunnableTool | null>(null);
  const [payload, setPayload] = useState("{}");
  const [events, setEvents] = useState<readonly ToolRunEvent[]>([]);
  const [active, setActive] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const unsubscribe = client.subscribe((state) => {
      setEvents(state.events.map((event) => ({
        sequence: event.sequence,
        kind: event.kind === "progress" ? "progress" : event.kind === "artifact" ? "artifact" : event.kind === "result" ? "result" : "log",
        message: event.message,
      })));
    });
    return () => { unsubscribe(); client.dispose(); };
  }, [client]);

  const select = (id: string) => {
    const tool = RUNNABLE_TOOLS.find((entry) => entry.id === id) ?? null;
    setSelected(tool);
    setPayload(JSON.stringify(tool?.defaultPayload ?? {}, null, 2));
    setEvents([]);
    setError(null);
  };

  const run = async () => {
    if (!selected) return;
    setBusy(true);
    setError(null);
    try {
      let parsed: unknown;
      try { parsed = JSON.parse(payload); }
      catch { throw new Error("Parameters must be valid JSON."); }
      if (!active) {
        const runId = `tool-${Date.now().toString(36)}`;
        await client.start(selected.id, runId);
        setActive(true);
      }
      await client.command(selected.commandId, parsed);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };

  const stop = async () => {
    setBusy(true);
    setError(null);
    try {
      await client.stop();
      setActive(false);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };

  return <section className="tool-workspace" aria-label="Hardware tools">
    <ToolCatalog tools={RUNNABLE_TOOLS} onSelect={select} />
    {selected && <section aria-label="Selected tool">
      <h2>{selected.id}</h2>
      <p>{selected.sample}</p>
      <label>Parameters<textarea value={payload} rows={7} spellCheck={false} onChange={(event) => setPayload(event.target.value)} disabled={busy || active} /></label>
      <div className="actions">
        <button type="button" onClick={() => void run()} disabled={busy}>{active ? "Run again" : "Start and run"}</button>
        <button type="button" className="secondary" onClick={() => void stop()} disabled={busy || !active}>Stop and release hardware</button>
      </div>
      {error && <p className="error" role="alert">{error}</p>}
      <ToolRunView events={events} onStop={active && !busy ? () => void stop() : undefined} />
    </section>}
  </section>;
}
