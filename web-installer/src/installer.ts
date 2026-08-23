import type { UpdateBundle } from "./archive";
import { FlipperRpcClient, RpcError, type DeviceIdentity } from "./flipper-rpc";
import {
  type SerialPortLike,
  type ByteTransport,
  WebSerialConnection,
  waitForAuthorizedFlipper,
} from "./web-serial";

const POST_INSTALL_TIMEOUT_MS = 10 * 60 * 1_000;
const STORAGE_RESERVE_BYTES = 8 * 1024 * 1024;
const EXPECTED_MODEL = "Flipper Zero";
const EXPECTED_TARGET = "7";
const EXPECTED_ORIGIN = "PoisonedOS";

export type InstallPhase =
  | "connecting"
  | "checking"
  | "uploading"
  | "verifying"
  | "preparing"
  | "rebooting"
  | "waiting"
  | "complete";

export interface InstallProgress {
  readonly phase: InstallPhase;
  readonly title: string;
  readonly detail: string;
  readonly completed: number;
  readonly total: number;
}

export interface InstallResult {
  readonly before: DeviceIdentity;
  readonly after: DeviceIdentity;
  readonly remoteManifest: string;
}

export type ProgressListener = (progress: InstallProgress) => void;

export interface InstallerConnection extends ByteTransport {
  readonly port: SerialPortLike;
  connect(signal?: AbortSignal): Promise<void>;
}

export interface InstallerRpcClient {
  verifySession(signal?: AbortSignal): Promise<DeviceIdentity>;
  freeSpace(path?: string, signal?: AbortSignal): Promise<bigint>;
  deleteTree(path: string, signal?: AbortSignal): Promise<void>;
  makeDirectory(path: string, signal?: AbortSignal): Promise<void>;
  writeFile(
    path: string,
    data: Uint8Array,
    progress?: (progress: { path: string; fileBytes: number; fileTotal: number }) => void,
    signal?: AbortSignal,
  ): Promise<void>;
  verifyFile(path: string, expectedSha256: string, expectedBytes: number, signal?: AbortSignal): Promise<void>;
  prepareUpdate(manifestPath: string, signal?: AbortSignal): Promise<void>;
  rebootToUpdate(signal?: AbortSignal): Promise<void>;
  close(): Promise<void>;
}

export interface InstallerDependencies {
  requestConnection(signal?: AbortSignal): Promise<InstallerConnection>;
  createClient(connection: InstallerConnection): InstallerRpcClient;
  waitForConnection(
    preferred: SerialPortLike,
    timeoutMs: number,
    onWait?: (elapsedMs: number) => void,
    signal?: AbortSignal,
  ): Promise<InstallerConnection>;
}

const DEFAULT_DEPENDENCIES: InstallerDependencies = {
  requestConnection: (signal) => WebSerialConnection.request(signal),
  createClient: (connection) => new FlipperRpcClient(connection),
  waitForConnection: (preferred, timeoutMs, onWait, signal) =>
    waitForAuthorizedFlipper(preferred, timeoutMs, onWait, signal),
};

function assertFlipperTarget(identity: DeviceIdentity): void {
  if (identity.hardwareModel !== EXPECTED_MODEL || identity.hardwareTarget !== EXPECTED_TARGET) {
    throw new RpcError(
      `Refusing to install on ${identity.hardwareModel || "unknown hardware"} target ${identity.hardwareTarget || "unknown"}; expected Flipper Zero target 7`,
    );
  }
}

function relativeBundlePath(bundle: UpdateBundle, path: string): string {
  const prefix = `${bundle.directory}/`;
  if (!path.startsWith(prefix)) throw new RpcError(`Update file escaped the package directory: ${path}`);
  return path.slice(prefix.length);
}

function parentDirectories(paths: readonly string[]): readonly string[] {
  const directories = new Set<string>();
  for (const path of paths) {
    const parts = path.split("/");
    parts.pop();
    let current = "";
    for (const part of parts) {
      current = current ? `${current}/${part}` : part;
      directories.add(current);
    }
  }
  return [...directories].sort((left, right) => left.split("/").length - right.split("/").length || left.localeCompare(right));
}

export class BrowserFirmwareInstaller {
  private connection: InstallerConnection | null = null;
  private client: InstallerRpcClient | null = null;
  private identity: DeviceIdentity | null = null;

  public constructor(private readonly dependencies: InstallerDependencies = DEFAULT_DEPENDENCIES) {}

  public get connectedIdentity(): DeviceIdentity | null {
    return this.identity;
  }

  public async connect(listener?: ProgressListener, signal?: AbortSignal): Promise<DeviceIdentity> {
    await this.disconnect();
    listener?.({ phase: "connecting", title: "Opening the cable", detail: "Choose the connected Flipper Zero in the browser prompt.", completed: 0, total: 1 });
    const connection = await this.dependencies.requestConnection(signal);
    await connection.connect(signal);
    const client = this.dependencies.createClient(connection);
    try {
      const identity = await client.verifySession(signal);
      assertFlipperTarget(identity);
      this.connection = connection;
      this.client = client;
      this.identity = identity;
      listener?.({ phase: "connecting", title: "Flipper identified", detail: `${identity.hardwareModel} · firmware ${identity.firmwareVersion || "unknown"} · RPC ${identity.protobufVersion}`, completed: 1, total: 1 });
      return identity;
    } catch (error) {
      await client.close().catch(() => undefined);
      throw error;
    }
  }

  public async install(
    bundle: UpdateBundle,
    listener?: ProgressListener,
    signal?: AbortSignal,
  ): Promise<InstallResult> {
    const connection = this.connection;
    const client = this.client;
    const before = this.identity;
    if (!connection || !client || !before) throw new RpcError("Connect and identify a Flipper Zero before installing");
    assertFlipperTarget(before);
    const existingPoisonedOs = before.firmwareOrigin === EXPECTED_ORIGIN;
    const remoteRoot = existingPoisonedOs
      ? `/ext/update/poison-web-${bundle.archiveSha256.slice(0, 12)}`
      : "/ext/update/poison-lkg";
    const remoteManifest = `${remoteRoot}/${relativeBundlePath(bundle, bundle.manifestPath)}`;
    let rebootStarted = false;
    let stagingStarted = false;
    try {
      listener?.({ phase: "checking", title: "Checking the SD card", detail: "Confirming space for a complete, verifiable update copy.", completed: 0, total: 1 });
      const freeSpace = await client.freeSpace("/ext", signal);
      const required = BigInt(bundle.totalBytes + STORAGE_RESERVE_BYTES);
      if (freeSpace < required) {
        throw new RpcError(`The SD card has ${freeSpace.toString()} free bytes; this install requires at least ${required.toString()}`);
      }
      await client.deleteTree(remoteRoot, signal);
      await client.makeDirectory("/ext/update", signal);
      await client.makeDirectory(remoteRoot, signal);
      stagingStarted = true;
      const relativeFiles = bundle.files.map((file) => relativeBundlePath(bundle, file.path));
      for (const directory of parentDirectories(relativeFiles)) {
        await client.makeDirectory(`${remoteRoot}/${directory}`, signal);
      }

      let uploaded = 0;
      for (const file of bundle.files) {
        const relative = relativeBundlePath(bundle, file.path);
        const path = `${remoteRoot}/${relative}`;
        await client.writeFile(path, file.data, ({ fileBytes }) => {
          listener?.({
            phase: "uploading",
            title: "Sending the firmware package",
            detail: relative,
            completed: uploaded + fileBytes,
            total: bundle.totalBytes,
          });
        }, signal);
        uploaded += file.data.byteLength;
      }

      let verified = 0;
      for (const file of bundle.files) {
        const relative = relativeBundlePath(bundle, file.path);
        listener?.({
          phase: "verifying",
          title: "Reading every file back",
          detail: relative,
          completed: verified,
          total: bundle.totalBytes,
        });
        await client.verifyFile(`${remoteRoot}/${relative}`, file.sha256, file.data.byteLength, signal);
        verified += file.data.byteLength;
      }
      listener?.({ phase: "preparing", title: "Preparing the updater", detail: "The Flipper is validating update.fuf and its target metadata.", completed: 0, total: 1 });
      await client.prepareUpdate(remoteManifest, signal);
      listener?.({ phase: "rebooting", title: "Starting the on-device updater", detail: "Keep the USB cable connected until verification finishes.", completed: 1, total: 1 });
      rebootStarted = true;
      await client.rebootToUpdate(signal);
      await connection.close().catch(() => undefined);
      this.client = null;
      this.connection = null;
      this.identity = null;

      const returned = await this.dependencies.waitForConnection(
        connection.port,
        POST_INSTALL_TIMEOUT_MS,
        (elapsed) => listener?.({
          phase: "waiting",
          title: "Poisoned_Os is installing",
          detail: `Waiting for the verified USB runtime to return · ${Math.floor(elapsed / 1_000)}s`,
          completed: elapsed,
          total: POST_INSTALL_TIMEOUT_MS,
        }),
        signal,
      );
      const verificationClient = this.dependencies.createClient(returned);
      try {
        const after = await verificationClient.verifySession(signal);
        assertFlipperTarget(after);
        if (after.firmwareOrigin !== EXPECTED_ORIGIN || !after.firmwareVersion) {
          throw new RpcError(`USB returned, but the device did not identify as PoisonedOS; origin was ${after.firmwareOrigin || "missing"}`);
        }
        this.connection = returned;
        this.client = verificationClient;
        this.identity = after;
        listener?.({ phase: "complete", title: "Poisoned_Os verified", detail: `Firmware ${after.firmwareVersion} is running on Flipper Zero target 7.`, completed: 1, total: 1 });
        return { before, after, remoteManifest };
      } catch (error) {
        await verificationClient.close().catch(() => undefined);
        throw error;
      }
    } catch (error) {
      if (!rebootStarted && stagingStarted) await client.deleteTree(remoteRoot, signal).catch(() => undefined);
      throw error;
    }
  }

  public async disconnect(): Promise<void> {
    const client = this.client;
    const connection = this.connection;
    this.client = null;
    this.connection = null;
    this.identity = null;
    if (client) await client.close().catch(() => undefined);
    else if (connection) await connection.close().catch(() => undefined);
  }
}
