import { describe, expect, it } from "vitest";
import { bridgeWebSocketProtocol, selectBridgeDevice } from "./BridgeWebSocketTransport";

describe("bridgeWebSocketProtocol", () => {
  it("encodes only the exact keyring token into the authenticated subprotocol", () => {
    const token = "a".repeat(64);
    expect(bridgeWebSocketProtocol(token)).toBe(`poisoned-os.rpc.v1.${token}`);
    expect(() => bridgeWebSocketProtocol("wrong")).toThrow(/64 lowercase hexadecimal/);
    expect(() => bridgeWebSocketProtocol("A".repeat(64))).toThrow(/64 lowercase hexadecimal/);
  });
});

describe("selectBridgeDevice", () => {
  const devices = [
    { id: "/dev/cu.usbmodem-test", label: "test", kind: "serial" as const, metadata: {} },
    { id: "/dev/cu.usbmodem-recovery", label: "recovery", kind: "serial" as const, metadata: {} },
  ];

  it("selects the exact HIL device and never falls back to a different device", () => {
    expect(selectBridgeDevice(devices, "/dev/cu.usbmodem-test")).toBe(devices[0]);
    expect(() => selectBridgeDevice(devices, "/dev/cu.usbmodem-missing")).toThrow(/requested bridge device/);
    expect(selectBridgeDevice(devices)).toBe(devices[0]);
  });
});
