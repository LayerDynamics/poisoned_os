import { describe, expect, it } from "vitest";
import { ServiceAddressRegistry } from "./service-address-registry";

describe("local Node service address registry", () => {
  it("keeps independent named process addresses and rejects collisions", () => {
    const registry = new ServiceAddressRegistry(() => 1_000);
    registry.register({
      name: "builder",
      url: "http://127.0.0.1:49101",
      ownerPid: 101,
      ttlMs: 5_000,
    });
    registry.register({
      name: "workloads",
      url: "http://127.0.0.1:49102",
      ownerPid: 102,
      ttlMs: 5_000,
    });

    expect(registry.snapshot()).toEqual([
      {
        name: "builder",
        url: "http://127.0.0.1:49101/",
        ownerPid: 101,
        expiresAtMs: 6_000,
      },
      {
        name: "workloads",
        url: "http://127.0.0.1:49102/",
        ownerPid: 102,
        expiresAtMs: 6_000,
      },
    ]);

    expect(() => registry.register({
      name: "evidence",
      url: "http://127.0.0.1:49101",
      ownerPid: 103,
      ttlMs: 5_000,
    })).toThrow(/already leased/i);
  });

  it("renews the owning process and expires dead process addresses", () => {
    let now = 5_000;
    const registry = new ServiceAddressRegistry(() => now);
    registry.register({
      name: "builder",
      url: "http://localhost:49101",
      ownerPid: 101,
      ttlMs: 1_000,
    });

    now = 5_500;
    registry.renew("builder", 101, 2_000);
    now = 7_499;
    expect(registry.snapshot()).toHaveLength(1);
    now = 7_500;
    expect(registry.snapshot()).toEqual([]);
  });

  it("rejects non-local, malformed, and impersonated registrations", () => {
    const registry = new ServiceAddressRegistry(() => 0);
    expect(() => registry.register({
      name: "builder",
      url: "https://example.com",
      ownerPid: 1,
      ttlMs: 1_000,
    })).toThrow(/local/i);
    expect(() => registry.register({
      name: "Bad Name",
      url: "http://127.0.0.1:49101",
      ownerPid: 1,
      ttlMs: 1_000,
    })).toThrow(/name/i);

    registry.register({
      name: "builder",
      url: "http://127.0.0.1:49101",
      ownerPid: 1,
      ttlMs: 1_000,
    });
    expect(() => registry.renew("builder", 2, 1_000)).toThrow(/owner/i);
    expect(() => registry.release("builder", 2)).toThrow(/owner/i);
  });
});
