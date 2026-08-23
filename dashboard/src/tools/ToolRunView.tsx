import { useEffect, useMemo, useState, type ReactElement } from "react";
import type { StructuredAppSession } from "../apps/StructuredAppClient";
import { ToolCatalog, type ToolCatalogEntry } from "./ToolCatalog";
import { ToolClient } from "./ToolClient";
import { NfcTool, type NfcRequest } from "./families/NfcTool";
import { LfRfidTool, type LfRfidRequest } from "./families/LfRfidTool";
import { IButtonTool, type IButtonRequest } from "./families/IButtonTool";
import { InfraredTool, type InfraredRequest } from "./families/InfraredTool";
import { SubGhzTool, type SubGhzRequest } from "./families/SubGhzTool";

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

export function infraredCommandForRequest(request: InfraredRequest): {
  readonly commandId: "infrared.receive" | "infrared.transmit";
  readonly payload: Readonly<Record<string, unknown>>;
} {
  return request.operation === "receive"
    ? {
        commandId: "infrared.receive",
        payload: { timeout_ms: request.timeoutMs, maximum_timings: request.maximumTimings },
      }
    : {
        commandId: "infrared.transmit",
        payload: { exact_confirmation: request.exactConfirmation },
      };
}

export function subGhzCommandForRequest(request: SubGhzRequest): {
  readonly commandId: "sub-ghz.receive" | "sub-ghz.analyze" | "sub-ghz.transmit";
  readonly payload: Readonly<Record<string, unknown>>;
} {
  if (request.operation === "transmit") {
    return {
      commandId: "sub-ghz.transmit",
      payload: { exact_confirmation: request.exactConfirmation },
    };
  }
  return {
    commandId: request.operation === "analyze" ? "sub-ghz.analyze" : "sub-ghz.receive",
    payload: {
      frequency_hz: request.frequencyHz,
      timeout_ms: request.timeoutMs,
      maximum_timings: request.maximumTimings,
    },
  };
}

const RUNNABLE_TOOLS: readonly RunnableTool[] = [
  { id: "nfc.read", family: "nfc", status: "foundation", purpose: "Detect supported NFC protocols without mutating the tag.", capabilities: ["nfc.read"], sample: "Present a tag for five seconds.", commandId: "nfc.detect", defaultPayload: { timeout_ms: 5000 } },
  { id: "lf-rfid.read", family: "lf-rfid", status: "foundation", purpose: "Detect and read a supported LF RFID credential.", capabilities: ["lf-rfid.read"], sample: "Present a credential for five seconds.", commandId: "lf-rfid.detect", defaultPayload: { timeout_ms: 5000 } },
  { id: "ibutton.read", family: "ibutton", status: "foundation", purpose: "Read an iButton or 1-Wire identifier.", capabilities: ["ibutton.read"], sample: "Touch a key to the contacts.", commandId: "ibutton.read", defaultPayload: { timeout_ms: 5000 } },
  { id: "infrared.receive", family: "infrared", status: "foundation", purpose: "Receive bounded infrared signal metadata and replay the current exact capture when separately authorized.", capabilities: ["infrared.receive", "infrared.transmit"], sample: "Point a remote and press a button.", commandId: "infrared.receive", defaultPayload: { timeout_ms: 5000, maximum_timings: 1024 } },
  { id: "sub-ghz.receive", family: "sub-ghz", status: "foundation", purpose: "Receive, analyze, and explicitly replay a Sub-GHz capture under live firmware policy.", capabilities: ["sub-ghz.receive", "sub-ghz.analyze", "sub-ghz.transmit"], sample: "Listen on a permitted fixture frequency.", commandId: "sub-ghz.receive", defaultPayload: { frequency_hz: 433920000, timeout_ms: 5000, maximum_timings: 1024 } },
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

  const runNfc = async (request: NfcRequest) => {
    if (!selected || selected.id !== "nfc.read" || request.operation !== "detect") {
      setError("The selected NFC operation is not exposed by the verified device adapter.");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      if (!active) {
        const runId = `tool-${Date.now().toString(36)}`;
        await client.start(selected.id, runId);
        setActive(true);
      }
      await client.command("nfc.detect", { timeout_ms: request.timeoutMs });
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };

  const runLfRfid = async (request: LfRfidRequest) => {
    if (!selected || selected.id !== "lf-rfid.read" || request.operation !== "read") {
      setError("The selected LF RFID operation is not exposed by the verified device adapter.");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      if (!active) {
        const runId = `tool-${Date.now().toString(36)}`;
        await client.start(selected.id, runId);
        setActive(true);
      }
      await client.command("lf-rfid.detect", { timeout_ms: request.timeoutMs });
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };

  const runIButton = async (request: IButtonRequest) => {
    if (!selected || selected.id !== "ibutton.read" || request.operation !== "read") {
      setError("The selected iButton operation is not exposed by the verified device adapter.");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      if (!active) {
        const runId = `tool-${Date.now().toString(36)}`;
        await client.start(selected.id, runId);
        setActive(true);
      }
      await client.command("ibutton.read", { timeout_ms: request.timeoutMs });
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };

  const runInfrared = async (request: InfraredRequest) => {
    if (!selected || selected.id !== "infrared.receive") {
      setError("The selected infrared operation is not exposed by the verified device adapter.");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      if (!active) {
        const runId = `tool-${Date.now().toString(36)}`;
        await client.start(selected.id, runId);
        setActive(true);
      }
      const command = infraredCommandForRequest(request);
      await client.command(command.commandId, command.payload);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : String(reason));
    } finally {
      setBusy(false);
    }
  };

  const runSubGhz = async (request: SubGhzRequest) => {
    if (!selected || selected.id !== "sub-ghz.receive") {
      setError("The selected Sub-GHz operation is not exposed by the verified device adapter.");
      return;
    }
    setBusy(true);
    setError(null);
    try {
      if (!active) {
        const runId = `tool-${Date.now().toString(36)}`;
        await client.start(selected.id, runId);
        setActive(true);
      }
      const command = subGhzCommandForRequest(request);
      await client.command(command.commandId, command.payload);
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
      {selected.id === "nfc.read" ? <NfcTool
        capabilities={{
          read: selected.capabilities.includes("nfc.read"),
          rawCapture: selected.capabilities.includes("nfc.raw-capture"),
          write: selected.capabilities.includes("nfc.write"),
          emulate: selected.capabilities.includes("nfc.emulate"),
        }}
        active={active}
        busy={busy}
        onRun={runNfc}
        onStop={stop}
      /> : selected.id === "lf-rfid.read" ? <LfRfidTool
        capabilities={{
          read: selected.capabilities.includes("lf-rfid.read"),
          write: selected.capabilities.includes("lf-rfid.write"),
          emulate: selected.capabilities.includes("lf-rfid.emulate"),
        }}
        active={active}
        busy={busy}
        onRun={runLfRfid}
        onStop={stop}
      /> : selected.id === "ibutton.read" ? <IButtonTool
        capabilities={{
          read: selected.capabilities.includes("ibutton.read"),
          write: selected.capabilities.includes("ibutton.write"),
          emulate: selected.capabilities.includes("ibutton.emulate"),
        }}
        active={active}
        busy={busy}
        onRun={runIButton}
        onStop={stop}
      /> : selected.id === "infrared.receive" ? <InfraredTool
        capabilities={{
          receive: selected.capabilities.includes("infrared.receive"),
          transmit: selected.capabilities.includes("infrared.transmit"),
        }}
        active={active}
        busy={busy}
        onRun={runInfrared}
        onStop={stop}
      /> : selected.id === "sub-ghz.receive" ? <SubGhzTool
        capabilities={{
          receive: selected.capabilities.includes("sub-ghz.receive"),
          analyze: selected.capabilities.includes("sub-ghz.analyze"),
          transmit: selected.capabilities.includes("sub-ghz.transmit"),
        }}
        active={active}
        busy={busy}
        onRun={runSubGhz}
        onStop={stop}
      /> : <>
        <label>Parameters<textarea value={payload} rows={7} spellCheck={false} onChange={(event) => setPayload(event.target.value)} disabled={busy || active} /></label>
        <div className="actions">
          <button type="button" onClick={() => void run()} disabled={busy}>{active ? "Run again" : "Start and run"}</button>
          <button type="button" className="secondary" onClick={() => void stop()} disabled={busy || !active}>Stop and release hardware</button>
        </div>
      </>}
      {error && <p className="error" role="alert">{error}</p>}
      <ToolRunView events={events} onStop={active && !busy ? () => void stop() : undefined} />
    </section>}
  </section>;
}
