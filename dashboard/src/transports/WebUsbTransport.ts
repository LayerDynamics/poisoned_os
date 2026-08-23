import { DiscoveredDevice, Transport, TransportError, TransportHealth, throwIfAborted } from "./Transport";

export class WebUsbTransport implements Transport {
  public readonly kind = "usb" as const;
  public readonly mtu = 1024;

  public get health(): TransportHealth {
    return {
      connected: false,
      writable: false,
      queuedBytes: 0,
      lastError: "Flipper runtime RPC uses USB CDC; select Web Serial or the local bridge",
    };
  }

  public async discover(signal?: AbortSignal): Promise<readonly DiscoveredDevice[]> {
    throwIfAborted(signal);
    throw new TransportError(
      "unsupported",
      "Flipper runtime RPC is a USB CDC interface; use Web Serial or the local bridge",
    );
  }

  public async connect(_device: DiscoveredDevice, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    throw new TransportError(
      "unsupported",
      "Flipper runtime RPC is not exposed through a WebUSB vendor interface",
    );
  }

  public async read(): Promise<Uint8Array | null> {
    throw new TransportError("not-connected", "WebUSB transport is not available for Flipper runtime RPC");
  }

  public async write(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (frame.byteLength > this.mtu) throw new TransportError("frame-too-large", "frame exceeds transport MTU");
    throw new TransportError("not-connected", "WebUSB transport is not available for Flipper runtime RPC");
  }

  public async close(): Promise<void> {}
}
