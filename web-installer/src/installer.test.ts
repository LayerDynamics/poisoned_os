import { describe, expect, it } from "vitest";
import { sha256, type UpdateBundle } from "./archive";
import {
  BrowserFirmwareInstaller,
  type InstallerConnection,
  type InstallerDependencies,
  type InstallerRpcClient,
} from "./installer";
import type { DeviceIdentity } from "./flipper-rpc";
import type { SerialPortLike } from "./web-serial";

const stockIdentity: DeviceIdentity = {
  hardwareModel: "Flipper Zero",
  hardwareTarget: "7",
  firmwareVersion: "1.4.3",
  firmwareOrigin: "",
  protobufVersion: "0.15",
  properties: {},
};

const poisonIdentity: DeviceIdentity = {
  ...stockIdentity,
  firmwareVersion: "1.0.0",
  firmwareOrigin: "PoisonedOS",
};

class FixturePort extends EventTarget implements SerialPortLike {
  public readable = null;
  public writable = null;
  public async open(): Promise<void> {}
  public async close(): Promise<void> {}
  public getInfo() { return { usbVendorId: 0x0483, usbProductId: 0x5740 }; }
}

class FixtureConnection implements InstallerConnection {
  public readonly port = new FixturePort();
  public connected = false;
  public closed = false;
  public async connect(): Promise<void> { this.connected = true; }
  public async read(): Promise<Uint8Array | null> { return null; }
  public async write(): Promise<void> {}
  public async close(): Promise<void> { this.closed = true; }
}

class FixtureClient implements InstallerRpcClient {
  public readonly deleted: string[] = [];
  public readonly directories: string[] = [];
  public readonly written: string[] = [];
  public readonly verified: string[] = [];
  public readonly verifiedBytes: number[] = [];
  public prepared = "";
  public rebooted = false;
  public closed = false;

  public constructor(
    private readonly identity: DeviceIdentity,
    public available = 64_000_000n,
  ) {}

  public async verifySession(): Promise<DeviceIdentity> { return this.identity; }
  public async freeSpace(): Promise<bigint> { return this.available; }
  public async deleteTree(path: string): Promise<void> { this.deleted.push(path); }
  public async makeDirectory(path: string): Promise<void> { this.directories.push(path); }
  public async writeFile(
    path: string,
    data: Uint8Array,
    progress?: (value: { path: string; fileBytes: number; fileTotal: number }) => void,
  ): Promise<void> {
    this.written.push(path);
    progress?.({ path, fileBytes: data.byteLength, fileTotal: data.byteLength });
  }
  public async verifyFile(path: string, expectedSha256: string, expectedBytes: number): Promise<void> {
    expect(expectedSha256).toMatch(/^[0-9a-f]{64}$/);
    this.verified.push(path);
    this.verifiedBytes.push(expectedBytes);
  }
  public async prepareUpdate(path: string): Promise<void> { this.prepared = path; }
  public async rebootToUpdate(): Promise<void> { this.rebooted = true; }
  public async close(): Promise<void> { this.closed = true; }
}

async function bundle(): Promise<UpdateBundle> {
  const manifest = new TextEncoder().encode("manifest");
  const firmware = Uint8Array.from([1, 2, 3, 4]);
  return {
    archiveSha256: "a".repeat(64),
    directory: "f7-update-poisonedos",
    manifestPath: "f7-update-poisonedos/update.fuf",
    totalBytes: manifest.byteLength + firmware.byteLength,
    versionLabel: "poisonedos-1.0.0",
    files: [
      { path: "f7-update-poisonedos/update.fuf", data: manifest, sha256: await sha256(manifest) },
      { path: "f7-update-poisonedos/firmware.dfu", data: firmware, sha256: await sha256(firmware) },
    ],
  };
}

function fixture(beforeIdentity: DeviceIdentity, afterIdentity = poisonIdentity) {
  const initialConnection = new FixtureConnection();
  const returnedConnection = new FixtureConnection();
  const beforeClient = new FixtureClient(beforeIdentity);
  const afterClient = new FixtureClient(afterIdentity);
  const dependencies: InstallerDependencies = {
    async requestConnection() { return initialConnection; },
    createClient(connection) { return connection === initialConnection ? beforeClient : afterClient; },
    async waitForConnection(preferred) {
      expect(preferred).toBe(initialConnection.port);
      returnedConnection.connected = true;
      return returnedConnection;
    },
  };
  return {
    installer: new BrowserFirmwareInstaller(dependencies),
    beforeClient,
    afterClient,
    initialConnection,
    returnedConnection,
  };
}

describe("complete browser firmware installation", () => {
  it("seeds the bootstrap last-known-good bundle on a stock Flipper and verifies PoisonedOS after reboot", async () => {
    const setup = fixture(stockIdentity);
    await setup.installer.connect();
    const result = await setup.installer.install(await bundle());

    expect(setup.beforeClient.deleted[0]).toBe("/ext/update/poison-lkg");
    expect(setup.beforeClient.written).toEqual([
      "/ext/update/poison-lkg/update.fuf",
      "/ext/update/poison-lkg/firmware.dfu",
    ]);
    expect(setup.beforeClient.verified).toEqual(setup.beforeClient.written);
    expect(setup.beforeClient.verifiedBytes).toEqual([8, 4]);
    expect(setup.beforeClient.prepared).toBe("/ext/update/poison-lkg/update.fuf");
    expect(setup.beforeClient.rebooted).toBe(true);
    expect(result.after.firmwareOrigin).toBe("PoisonedOS");
    expect(setup.installer.connectedIdentity).toEqual(poisonIdentity);
  });

  it("uses a digest-specific staging directory when PoisonedOS already owns rollback state", async () => {
    const setup = fixture(poisonIdentity);
    await setup.installer.connect();
    const result = await setup.installer.install(await bundle());
    expect(result.remoteManifest).toBe("/ext/update/poison-web-aaaaaaaaaaaa/update.fuf");
    expect(setup.beforeClient.deleted[0]).toBe("/ext/update/poison-web-aaaaaaaaaaaa");
    expect(setup.beforeClient.deleted).not.toContain("/ext/update/poison-lkg");
  });

  it("fails before writing when the SD card cannot retain the complete update and reserve", async () => {
    const setup = fixture(stockIdentity);
    setup.beforeClient.available = 1n;
    await setup.installer.connect();
    await expect(setup.installer.install(await bundle())).rejects.toThrowError(/requires at least/);
    expect(setup.beforeClient.written).toEqual([]);
    expect(setup.beforeClient.deleted).toEqual([]);
  });

  it("does not report success when the rebooted device lacks the PoisonedOS identity", async () => {
    const setup = fixture(stockIdentity, stockIdentity);
    await setup.installer.connect();
    await expect(setup.installer.install(await bundle())).rejects.toThrowError(/did not identify as PoisonedOS/);
    expect(setup.installer.connectedIdentity).toBeNull();
    expect(setup.afterClient.closed).toBe(true);
  });

  it("closes the returned USB session when post-install target verification fails", async () => {
    const setup = fixture(stockIdentity, { ...poisonIdentity, hardwareTarget: "99" });
    await setup.installer.connect();
    await expect(setup.installer.install(await bundle())).rejects.toThrowError(/expected Flipper Zero target 7/);
    expect(setup.afterClient.closed).toBe(true);
    expect(setup.installer.connectedIdentity).toBeNull();
  });
});
