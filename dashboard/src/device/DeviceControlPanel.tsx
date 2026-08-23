import { useEffect, useRef, useState, type FormEvent, type KeyboardEvent, type ReactElement } from "react";
import { DeviceControlClient, type DeviceControlSession } from "./DeviceControlClient";
import type { InputKey } from "./InputController";
import { RemoteScreen, type ScreenFrame } from "./RemoteScreen";

export function DeviceControlPanel({ session }: { session: DeviceControlSession }): ReactElement {
  const [frame, setFrame] = useState<ScreenFrame | null>(null);
  const [status, setStatus] = useState("starting encrypted screen stream");
  const [appName, setAppName] = useState("NFC");
  const clientRef = useRef<DeviceControlClient | null>(null);
  const inputRef = useRef<ReturnType<DeviceControlClient["inputController"]> | null>(null);

  useEffect(() => {
    const client = new DeviceControlClient(session, {
      onFrame: (next) => { setFrame(next); setStatus(`frame ${next.sequence}`); },
      onAppState: (state) => setStatus(`application ${state}`),
      onError: (error) => setStatus(error.message),
    });
    const input = client.inputController();
    clientRef.current = client;
    inputRef.current = input;
    void client.startScreenStream().catch((error) =>
      setStatus(error instanceof Error ? error.message : String(error)));
    return () => {
      clientRef.current = null;
      inputRef.current = null;
      void input.close(Date.now());
      void client.dispose();
    };
  }, [session]);

  const send = async (key: InputKey) => {
    try {
      if (!inputRef.current) throw new Error("device control is not ready");
      await inputRef.current.short(key, Date.now());
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error));
    }
  };

  const keyboard = (event: KeyboardEvent<HTMLElement>) => {
    const keys: Readonly<Record<string, InputKey>> = {
      ArrowUp: "up", ArrowDown: "down", ArrowLeft: "left", ArrowRight: "right",
      Enter: "ok", Escape: "back",
    };
    const key = keys[event.key];
    if (!key) return;
    event.preventDefault();
    void send(key);
  };

  const launch = async (event: FormEvent) => {
    event.preventDefault();
    try {
      setStatus(`launching ${appName}`);
      if (!clientRef.current) throw new Error("device control is not ready");
      await clientRef.current.launchApp(appName);
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error));
    }
  };

  return <section className="update-card device-control" aria-labelledby="device-control-title" tabIndex={0} onKeyDown={keyboard}>
    <div className="update-heading">
      <div><p className="eyebrow">ENCRYPTED LIVE DEVICE CONTROL</p><h2 id="device-control-title">Flipper screen</h2></div>
      <output className="update-state" aria-live="polite">{status}</output>
    </div>
    <RemoteScreen frame={frame} />
    <div className="device-keys" aria-label="Flipper controls">
      <button type="button" aria-label="Up" onClick={() => void send("up")}>↑</button>
      <button type="button" aria-label="Left" onClick={() => void send("left")}>←</button>
      <button type="button" aria-label="OK" onClick={() => void send("ok")}>OK</button>
      <button type="button" aria-label="Right" onClick={() => void send("right")}>→</button>
      <button type="button" aria-label="Down" onClick={() => void send("down")}>↓</button>
      <button type="button" aria-label="Back" onClick={() => void send("back")}>Back</button>
    </div>
    <form onSubmit={(event) => void launch(event)}>
      <input aria-label="Application name" value={appName} onChange={(event) => setAppName(event.target.value)} />
      <button type="submit" disabled={!appName}>Launch app</button>
      <button type="button" className="secondary" onClick={() => void clientRef.current?.exitApp().catch((error) => setStatus(error instanceof Error ? error.message : String(error)))}>Exit RPC app</button>
    </form>
  </section>;
}
