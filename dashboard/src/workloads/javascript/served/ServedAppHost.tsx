import { useEffect, useRef, useState, type ReactElement } from "react";
import {
  createBrokerNonce,
  MessageBroker,
  type BrokerCapability,
  type BrokerRequest,
} from "./MessageBroker";
import { materializeServedBundle, type ServedBundlePayload } from "./BundleVerifier";

export interface ServedAppHostProps {
  readonly loadBundle: () => Promise<ServedBundlePayload>;
  readonly capabilities?: readonly BrokerCapability[];
  readonly authorize?: (capability: BrokerCapability) => boolean | Promise<boolean>;
  readonly dispatch?: (request: BrokerRequest, signal: AbortSignal) => Promise<unknown>;
}

const supportedCapabilities = new Set<BrokerCapability>([
  "device.status.read",
  "device.app.run",
  "evidence.create",
]);
const noCapabilities: readonly BrokerCapability[] = [];

export function ServedAppHost({
  loadBundle,
  capabilities = noCapabilities,
  authorize = () => false,
  dispatch = async () => { throw new Error("served interface operations are unavailable"); },
}: ServedAppHostProps): ReactElement {
  const [url, setUrl] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const iframe = useRef<HTMLIFrameElement | null>(null);
  const broker = useRef<MessageBroker | null>(null);
  useEffect(() => {
    let disposed = false;
    let revoke: (() => void) | undefined;
    let validationId = 0;
    const pending = new Set<number>();
    const abort = new AbortController();
    const worker = new Worker(new URL("./served-worker.ts", import.meta.url), { type: "module" });
    const onMessage = (event: MessageEvent) => {
      const active = broker.current;
      if (!active?.acceptsEnvelope(event)) return;
      const id = validationId++;
      pending.add(id);
      try {
        worker.postMessage({ validationId: id, data: event.data });
      } catch {
        pending.delete(id);
      }
    };
    worker.addEventListener("message", (event: MessageEvent<{ validationId: number; valid: boolean; data: unknown }>) => {
      const active = broker.current;
      const validation = event.data;
      if (disposed || !active || !pending.delete(validation.validationId) || !validation.valid) return;
      let request: BrokerRequest;
      try {
        request = active.accept(validation.data);
      } catch {
        return;
      }
      void Promise.resolve(authorize(request.capability)).then(async (authorized) => {
        if (!authorized || abort.signal.aborted) throw new Error("active session denied broker capability");
        return dispatch(request, abort.signal);
      }).then((result) => {
        if (!disposed) active.reply(request, { ok: true, result });
      }).catch((reason: unknown) => {
        if (!disposed) active.reply(request, {
          ok: false,
          error: (reason instanceof Error ? reason.message : String(reason)).slice(0, 160),
        });
      });
    });
    window.addEventListener("message", onMessage);
    void loadBundle().then(async (bundle) => {
      const requested = bundle.metadata.requestedCapabilities;
      const allowed = new Set(capabilities);
      if (requested.some((capability) =>
        !supportedCapabilities.has(capability as BrokerCapability) ||
        !allowed.has(capability as BrokerCapability))) {
        throw new Error("served bundle requested an unavailable broker capability");
      }
      const nonce = createBrokerNonce();
      broker.current = new MessageBroker(nonce, new Set(requested as readonly BrokerCapability[]));
      const materialized = await materializeServedBundle(bundle, { brokerNonce: nonce });
      if (disposed) materialized.revoke();
      else {
        revoke = materialized.revoke;
        setUrl(materialized.url);
      }
    }).catch((reason: unknown) => {
      if (!disposed) setError(reason instanceof Error ? reason.message : String(reason));
    });
    return () => {
      disposed = true;
      abort.abort();
      pending.clear();
      window.removeEventListener("message", onMessage);
      worker.terminate();
      broker.current?.close();
      broker.current = null;
      revoke?.();
      setUrl(null);
    };
  }, [loadBundle, capabilities, authorize, dispatch]);
  if (error) return <p role="alert">Unable to load device interface: {error}</p>;
  if (!url) return <p role="status">Loading verified device interface…</p>;
  return <iframe
    ref={iframe}
    title="Device interface"
    src={url}
    sandbox="allow-scripts"
    referrerPolicy="no-referrer"
    onLoad={() => {
      const source = iframe.current?.contentWindow;
      if (!source) return;
      try { broker.current?.bind(source); }
      catch { setError("served interface broker binding failed"); }
    }}
  />;
}
