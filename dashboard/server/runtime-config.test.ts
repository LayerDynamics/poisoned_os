import { describe, expect, it } from "vitest";
import { loadRuntimeConfig } from "./runtime-config";

describe("Poisoned_Os runtime configuration", () => {
  it("keeps the default HTTP listener on loopback", () => {
    const config = loadRuntimeConfig({});
    expect(config.listen).toEqual({ host: "127.0.0.1", port: 55_173 });
    expect(config.publicUrl.href).toBe("http://127.0.0.1:55173/");
  });

  it("uses the web runtime as the primary route over a mandatory Wi-Fi board", () => {
    const config = loadRuntimeConfig({
      POISON_RUNTIME_LISTEN: "0.0.0.0:55173",
      POISON_RUNTIME_PUBLIC_URL: "https://192.168.4.2:55173",
      POISON_RUNTIME_TLS_CERT: "/etc/poisoned-os/runtime.crt",
      POISON_RUNTIME_TLS_KEY: "/etc/poisoned-os/runtime.key",
      POISON_WIFI_BOARDS: JSON.stringify({
        field: "http://blackmagic.local",
        lab: "http://192.168.4.44",
      }),
      POISON_NODE_SERVICES: JSON.stringify({
        builder: "http://127.0.0.1:49101",
        workloads: "http://127.0.0.1:49102",
      }),
    });

    expect(config.primaryRoute).toBe("web");
    expect(config.listen).toEqual({ host: "0.0.0.0", port: 55_173 });
    expect(config.publicUrl.href).toBe("https://192.168.4.2:55173/");
    expect(config.boards.map((board) => [board.id, board.httpUrl.href, board.tcpPort])).toEqual([
      ["field", "http://blackmagic.local/", 3456],
      ["lab", "http://192.168.4.44/", 3456],
    ]);
    expect(config.services).toEqual({
      builder: "http://127.0.0.1:49101/",
      workloads: "http://127.0.0.1:49102/",
    });
  });

  it("rejects ambiguous listeners, duplicate board addresses, and remote Node services", () => {
    expect(() => loadRuntimeConfig({ POISON_RUNTIME_LISTEN: "localhost" })).toThrow(/host:port/i);
    expect(() => loadRuntimeConfig({ POISON_RUNTIME_LISTEN: "0.0.0.0:55173" })).toThrow(/requires HTTPS/i);
    expect(() => loadRuntimeConfig({
      POISON_WIFI_BOARDS: JSON.stringify({
        field: "http://blackmagic.local",
        duplicate: "http://blackmagic.local",
      }),
    })).toThrow(/duplicate/i);
    expect(() => loadRuntimeConfig({
      POISON_NODE_SERVICES: JSON.stringify({ builder: "https://example.com" }),
    })).toThrow(/local/i);
  });

  it("makes HTTPS configuration explicit and complete", () => {
    const config = loadRuntimeConfig({
      POISON_RUNTIME_PUBLIC_URL: "https://poisoned.local:55173",
      POISON_RUNTIME_TLS_CERT: "/etc/poisoned-os/runtime.crt",
      POISON_RUNTIME_TLS_KEY: "/etc/poisoned-os/runtime.key",
    });
    expect(config.tls).toEqual({
      certPath: "/etc/poisoned-os/runtime.crt",
      keyPath: "/etc/poisoned-os/runtime.key",
    });
    expect(() => loadRuntimeConfig({
      POISON_RUNTIME_PUBLIC_URL: "https://poisoned.local:55173",
    })).toThrow(/HTTPS requires/i);
    expect(() => loadRuntimeConfig({
      POISON_RUNTIME_TLS_CERT: "/etc/poisoned-os/runtime.crt",
    })).toThrow(/configured together/i);
  });
});
