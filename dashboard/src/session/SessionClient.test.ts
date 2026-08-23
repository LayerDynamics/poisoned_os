import { create, fromBinary, toBinary } from "@bufbuild/protobuf";
import { describe, expect, it } from "vitest";
import { CommandStatus, MainSchema, type Main } from "../generated/flipper_pb";
import { ChannelOpenedSchema, PairingChallengeSchema, ResumeResponseSchema, SessionEnvelopeSchema } from "../generated/poison_session_pb";
import { Transport, TransportError, TransportHealth } from "../transports/Transport";
import { SessionClient } from "./SessionClient";

const encoder = new TextEncoder();

function bytes(...parts: Uint8Array[]): Uint8Array {
  const result = new Uint8Array(parts.reduce((length, part) => length + part.byteLength, 0));
  let offset = 0;
  for (const part of parts) {
    result.set(part, offset);
    offset += part.byteLength;
  }
  return result;
}

function u32(value: number): Uint8Array {
  const result = new Uint8Array(4);
  new DataView(result.buffer).setUint32(0, value, false);
  return result;
}

function u64(value: bigint): Uint8Array {
  const result = new Uint8Array(8);
  new DataView(result.buffer).setBigUint64(0, value, false);
  return result;
}

function frame(payload: Uint8Array): Uint8Array {
  const prefix: number[] = [];
  let remaining = payload.byteLength;
  do {
    let next = remaining & 0x7f;
    remaining >>>= 7;
    if (remaining !== 0) next |= 0x80;
    prefix.push(next);
  } while (remaining !== 0);
  return bytes(new Uint8Array(prefix), payload);
}

function unframe(input: Uint8Array): Uint8Array {
  let length = 0;
  let shift = 0;
  for (let offset = 0; offset < input.byteLength && offset < 5; offset += 1) {
    const octet = input[offset];
    length |= (octet & 0x7f) << shift;
    if ((octet & 0x80) === 0) return input.slice(offset + 1, offset + 1 + length);
    shift += 7;
  }
  throw new Error("invalid protobuf frame");
}

function aad(sessionId: bigint, sequence: bigint, acknowledgement: bigint, channel: string): Uint8Array {
  return bytes(u32(2), u64(sessionId), u64(sequence), u64(acknowledgement), encoder.encode(`${channel}\0`));
}

function iv(sessionId: bigint, sequence: bigint): Uint8Array {
  return bytes(u32(Number(sessionId >> 32n)), u64(sequence));
}

function equal(left: Uint8Array, right: Uint8Array): boolean {
  if (left.byteLength !== right.byteLength) return false;
  let difference = 0;
  for (let index = 0; index < left.byteLength; index += 1) difference |= left[index] ^ right[index];
  return difference === 0;
}

function exactBuffer(value: Uint8Array): ArrayBuffer {
  return value.slice().buffer as ArrayBuffer;
}

async function sha256(value: Uint8Array): Promise<Uint8Array> {
  return new Uint8Array(await crypto.subtle.digest("SHA-256", exactBuffer(value)));
}

async function aesGcm(
  operation: "encrypt" | "decrypt",
  keyBytes: Uint8Array,
  sessionId: bigint,
  sequence: bigint,
  acknowledgement: bigint,
  channel: string,
  input: Uint8Array,
): Promise<Uint8Array> {
  const key = await crypto.subtle.importKey("raw", exactBuffer(keyBytes), "AES-GCM", false, [operation]);
  return new Uint8Array(await crypto.subtle[operation]({
    name: "AES-GCM",
    iv: exactBuffer(iv(sessionId, sequence)),
    additionalData: exactBuffer(aad(sessionId, sequence, acknowledgement, channel)),
    tagLength: 128,
  }, key, exactBuffer(input)));
}

class CryptographicDeviceTransport implements Transport {
  public readonly kind = "usb" as const;
  public readonly mtu = 2048;
  public readonly requests: Uint8Array[] = [];
  public tamperTranscript = false;
  public responsePayload: Uint8Array | null = null;
  public failWrites = false;
  public stopped = false;
  public identityTrusted = false;
  private connected = false;
  private readonly reads: Uint8Array[] = [];
  private deviceKey: CryptoKeyPair | null = null;
  private clientPublic: Uint8Array = new Uint8Array();
  private clientNonce: Uint8Array = new Uint8Array();
  private readonly deviceNonce = new Uint8Array(32).map((_, index) => index + 33);
  private transcriptDigest: Uint8Array = new Uint8Array();
  private transcript: Uint8Array = new Uint8Array();
  private identityPublic: Uint8Array = new Uint8Array();
  private confirmationCode = "";
  private clientToDevice: Uint8Array = new Uint8Array();
  private deviceToClient: Uint8Array = new Uint8Array();
  private readonly sessionId = 0x1020304050607080n;
  private responseSequence = 0n;
  private readonly disconnectHandlers = new Set<() => void>();
  private resumeToken = new Uint8Array(32).fill(0x51);

  public get health(): TransportHealth {
    return { connected: this.connected, writable: this.connected, queuedBytes: 0 };
  }

  public async discover(): Promise<readonly never[]> { return []; }
  public async connect(): Promise<void> { this.connected = true; }
  public async close(): Promise<void> { this.connected = false; }
  public onDisconnect(handler: () => void): () => void {
    this.disconnectHandlers.add(handler);
    return () => this.disconnectHandlers.delete(handler);
  }
  public simulateDisconnect(): void {
    this.connected = false;
    for (const handler of this.disconnectHandlers) handler();
  }
  public async read(): Promise<Uint8Array | null> { return this.reads.shift() ?? null; }

  public async write(encodedFrame: Uint8Array): Promise<void> {
    if (!this.connected) throw new Error("not connected");
    if (this.failWrites) throw new TransportError("io", "physical write failed");
    const message = fromBinary(MainSchema, unframe(encodedFrame));
    if (message.content.case === "poisonPairingHello") {
      const hello = message.content.value;
      this.clientPublic = hello.clientEphemeralPublicKey.slice();
      this.identityPublic = hello.clientIdentityPublicKey.slice();
      this.clientNonce = hello.clientNonce.slice();
      this.deviceKey = await crypto.subtle.generateKey(
        { name: "ECDH", namedCurve: "P-256" },
        true,
        ["deriveBits"],
      );
      const devicePublic = new Uint8Array(await crypto.subtle.exportKey("raw", this.deviceKey.publicKey));
      const expiresAt = 60_000n;
      this.transcript = bytes(
        u32(hello.protocolVersion),
        hello.clientEphemeralPublicKey,
        hello.clientIdentityPublicKey,
        encoder.encode(`${hello.clientName}\0`),
        u32(hello.requestedRole),
        u32(hello.requestedCapabilities),
        hello.clientNonce,
        devicePublic,
        this.deviceNonce,
        u64(expiresAt),
        u64(this.sessionId),
      );
      this.transcriptDigest = await sha256(this.transcript);
      this.confirmationCode = String(new DataView(this.transcriptDigest.buffer).getUint32(0, false) % 1_000_000).padStart(6, "0");
      const advertisedDigest = this.transcriptDigest.slice();
      if (this.tamperTranscript) advertisedDigest[0] ^= 0x80;
      const challenge = create(PairingChallengeSchema, {
        protocolVersion: 2,
        deviceEphemeralPublicKey: devicePublic,
        deviceNonce: this.deviceNonce,
        confirmationCode: this.confirmationCode,
        transcriptDigest: advertisedDigest,
        expiresAtMs: expiresAt,
        sessionId: this.sessionId,
        identityTrusted: this.identityTrusted,
      });
      this.reads.push(frame(toBinary(MainSchema, create(MainSchema, {
        commandId: message.commandId,
        commandStatus: CommandStatus.OK,
        content: { case: "poisonPairingChallenge", value: challenge },
      }))));
      return;
    }
    if (message.content.case === "poisonPairingConfirm") {
      const confirm = message.content.value;
      if (!confirm.physicalConfirmation || confirm.confirmationCode !== this.confirmationCode ||
          !equal(confirm.transcriptDigest, this.transcriptDigest)) {
        throw new Error("invalid pairing confirmation");
      }
      const identityKey = await crypto.subtle.importKey(
        "raw",
        exactBuffer(this.identityPublic),
        { name: "ECDSA", namedCurve: "P-256" },
        false,
        ["verify"],
      );
      if (!(await crypto.subtle.verify(
        { name: "ECDSA", hash: "SHA-256" },
        identityKey,
        exactBuffer(confirm.clientIdentitySignature),
        exactBuffer(this.transcript),
      ))) throw new Error("invalid client identity signature");
      await this.deriveKeys();
      this.reads.push(frame(toBinary(MainSchema, create(MainSchema, {
        commandId: message.commandId,
        commandStatus: CommandStatus.OK,
        content: { case: "empty", value: {} },
      }))));
      return;
    }
    if (message.content.case === "poisonResumeRequest") {
      const request = message.content.value;
      const accepted = request.sessionId === this.sessionId &&
        equal(request.resumeToken, this.resumeToken) &&
        request.lastReceivedSequence + 1n === this.responseSequence;
      if (accepted) this.resumeToken = new Uint8Array(32).fill(0x62);
      this.reads.push(frame(toBinary(MainSchema, create(MainSchema, {
        commandId: message.commandId,
        commandStatus: CommandStatus.OK,
        content: {
          case: "poisonResumeResponse",
          value: create(ResumeResponseSchema, {
            accepted,
            resumeToken: accepted ? this.resumeToken : new Uint8Array(),
            nextSequence: this.responseSequence,
            error: accepted ? "" : "resume-refused",
          }),
        },
      }))));
      return;
    }
    if (message.content.case !== "poisonSessionEnvelope") throw new Error("unexpected RPC content");
    const envelope = message.content.value;
    const plaintext = await aesGcm(
      "decrypt",
      this.clientToDevice,
      this.sessionId,
      envelope.sequence,
      envelope.acknowledgement,
      envelope.channel,
      bytes(envelope.payload, envelope.authenticationTag),
    );
    let responsePayload = this.responsePayload ?? plaintext;
    let innerRequest: Main | null = null;
    try { innerRequest = fromBinary(MainSchema, plaintext); } catch { /* Raw send()/receive() fixture. */ }
    if (innerRequest?.content.case === "poisonResumeRequest") {
      this.resumeToken = new Uint8Array(32).fill(0x51);
      responsePayload = toBinary(MainSchema, create(MainSchema, {
        commandId: innerRequest.commandId,
        commandStatus: CommandStatus.OK,
        content: {
          case: "poisonResumeResponse",
          value: create(ResumeResponseSchema, {
            accepted: true,
            resumeToken: this.resumeToken,
            nextSequence: this.responseSequence + 1n,
          }),
        },
      }));
    } else if (innerRequest?.content.case === "poisonChannelOpen") {
      responsePayload = toBinary(MainSchema, create(MainSchema, {
        commandId: innerRequest.commandId,
        commandStatus: CommandStatus.OK,
        content: {
          case: "poisonChannelOpened",
          value: create(ChannelOpenedSchema, {
            channel: innerRequest.content.value.channel,
            grantedCredits: innerRequest.content.value.initialCredits,
            nextSequence: innerRequest.content.value.resumeSequence,
          }),
        },
      }));
    } else if (innerRequest?.content.case === "stopSession") {
      this.stopped = true;
      responsePayload = toBinary(MainSchema, create(MainSchema, {
        commandId: innerRequest.commandId,
        commandStatus: CommandStatus.OK,
        content: { case: "empty", value: {} },
      }));
    } else {
      this.requests.push(plaintext);
    }
    const encryptedResponse = await aesGcm(
      "encrypt",
      this.deviceToClient,
      this.sessionId,
      this.responseSequence,
      envelope.sequence,
      envelope.channel,
      responsePayload,
    );
    const response = create(SessionEnvelopeSchema, {
      protocolVersion: 2,
      sessionId: this.sessionId,
      sequence: this.responseSequence,
      acknowledgement: envelope.sequence,
      channel: envelope.channel,
      payload: encryptedResponse.slice(0, -16),
      authenticationTag: encryptedResponse.slice(-16),
    });
    this.reads.push(frame(toBinary(MainSchema, create(MainSchema, {
      commandId: message.commandId,
      commandStatus: CommandStatus.OK,
      content: { case: "poisonSessionEnvelope", value: response },
    }))));
    this.responseSequence += 1n;
  }

  private async deriveKeys(): Promise<void> {
    if (!this.deviceKey) throw new Error("pairing was not initialized");
    const clientPublic = await crypto.subtle.importKey(
      "raw",
      exactBuffer(this.clientPublic),
      { name: "ECDH", namedCurve: "P-256" },
      false,
      [],
    );
    const shared = await crypto.subtle.deriveBits(
      { name: "ECDH", public: clientPublic },
      this.deviceKey.privateKey,
      256,
    );
    const hkdfKey = await crypto.subtle.importKey("raw", shared, "HKDF", false, ["deriveBits"]);
    const material = new Uint8Array(await crypto.subtle.deriveBits({
      name: "HKDF",
      hash: "SHA-256",
      salt: exactBuffer(bytes(this.clientNonce, this.deviceNonce)),
      info: exactBuffer(bytes(encoder.encode("poison-rpc-v2"), this.transcriptDigest)),
    }, hkdfKey, 512));
    this.clientToDevice = material.slice(0, 32);
    this.deviceToClient = material.slice(32);
  }
}

describe("SessionClient", () => {
  it("pairs, derives directional keys, and exchanges authenticated encrypted RPC frames", async () => {
    const transport = new CryptographicDeviceTransport();
    const client = new SessionClient();
    await client.connect(transport, { id: "usb-0", label: "test", kind: "usb", metadata: {} }, {
      clientName: "field-console",
      requestedRole: 1,
      requestedCapabilities: 1,
      approve: async ({ confirmationCode }) => confirmationCode.length === 6,
    });

    const request = new Uint8Array([0x08, 0x2a, 0x2a, 0x00]);
    await client.send("rpc", request);
    const response = await client.receive();
    expect(response.payload).toEqual(request);
    expect(transport.requests).toEqual([request]);
    expect(client.status).toBe("active");

    await client.close();
    expect(client.status).toBe("disconnected");
    expect(client.id).toBe(0n);
  });

  it("rejects a challenge whose transcript was changed in transit", async () => {
    const transport = new CryptographicDeviceTransport();
    transport.tamperTranscript = true;
    const client = new SessionClient();
    await expect(client.connect(transport, { id: "usb-0", label: "test", kind: "usb", metadata: {} }, {
      clientName: "field-console",
      requestedRole: 1,
      requestedCapabilities: 1,
      approve: async () => true,
    })).rejects.toMatchObject({ code: "protocol" });
    expect(client.status).toBe("disconnected");
  });

  it("uses a valid stored client identity without repeating physical approval", async () => {
    const transport = new CryptographicDeviceTransport();
    transport.identityTrusted = true;
    const client = new SessionClient();
    let approvalCalls = 0;
    await client.connect(transport, { id: "usb-0", label: "test", kind: "usb", metadata: {} }, {
      clientName: "field-console",
      requestedRole: 1,
      requestedCapabilities: 1,
      approve: async () => {
        approvalCalls += 1;
        return false;
      },
    });
    expect(approvalCalls).toBe(0);
    expect(client.status).toBe("active");
  });

  it("serializes concurrent RPC transactions on one encrypted receive stream", async () => {
    const transport = new CryptographicDeviceTransport();
    const client = new SessionClient();
    await client.connect(transport, { id: "usb-0", label: "test", kind: "usb", metadata: {} }, {
      clientName: "field-console",
      requestedRole: 1,
      requestedCapabilities: 1,
      approve: async () => true,
    });
    const first = create(MainSchema, { commandId: 41, content: { case: "empty", value: {} } });
    const second = create(MainSchema, { commandId: 42, content: { case: "empty", value: {} } });

    const [firstResponse, secondResponse] = await Promise.all([
      client.request(first),
      client.request(second),
    ]);

    expect(firstResponse.commandId).toBe(41);
    expect(secondResponse.commandId).toBe(42);
    expect(transport.requests.map((request) => fromBinary(MainSchema, request).commandId)).toEqual([41, 42]);
  });

  it("accepts framebuffer-sized encrypted responses and rejects responses above the device bound", async () => {
    const transport = new CryptographicDeviceTransport();
    const client = new SessionClient();
    await client.connect(transport, { id: "usb-0", label: "test", kind: "usb", metadata: {} }, {
      clientName: "field-console",
      requestedRole: 1,
      requestedCapabilities: 1,
      approve: async () => true,
    });

    const framebufferResponse = new Uint8Array(1_100).fill(0xa5);
    transport.responsePayload = framebufferResponse;
    await client.send("rpc", new Uint8Array([0x01]));
    await expect(client.receive()).resolves.toMatchObject({ payload: framebufferResponse });

    transport.responsePayload = new Uint8Array(1_281);
    await client.send("rpc", new Uint8Array([0x02]));
    await expect(client.receive()).rejects.toMatchObject({ code: "protocol" });
    expect(client.status).toBe("disconnected");
    expect(client.id).toBe(0n);
  });

  it("suspends an idle secure session and resumes it without repeating physical pairing", async () => {
    const transport = new CryptographicDeviceTransport();
    const client = new SessionClient();
    const statuses: string[] = [];
    client.onStatus((status) => statuses.push(status));
    await client.connect(transport, { id: "usb-0", label: "test", kind: "usb", metadata: {} }, {
      clientName: "field-console",
      requestedRole: 1,
      requestedCapabilities: 1,
      approve: async () => true,
    });

    transport.simulateDisconnect();
    await new Promise((resolve) => setTimeout(resolve, 0));

    expect(client.status).toBe("suspended");
    expect(client.id).not.toBe(0n);
    expect(client.transportHealth).toBeNull();
    expect(statuses).toEqual(["connecting", "negotiating", "active", "suspended"]);
    await expect(client.send("rpc", Uint8Array.from([1]))).rejects.toMatchObject({ code: "state" });

    await client.resume(transport, { id: "usb-0", label: "test", kind: "usb", metadata: {} });
    expect(client.status).toBe("active");
    expect(statuses.slice(-2)).toEqual(["resuming", "active"]);
    const response = await client.request(create(MainSchema, {
      commandId: 77,
      content: { case: "empty", value: {} },
    }));
    expect(response.commandId).toBe(77);
  });

  it("invalidates the secure session when a direct send loses the transport", async () => {
    const transport = new CryptographicDeviceTransport();
    const client = new SessionClient();
    await client.connect(transport, { id: "usb-0", label: "test", kind: "usb", metadata: {} }, {
      clientName: "field-console",
      requestedRole: 1,
      requestedCapabilities: 1,
      approve: async () => true,
    });
    transport.failWrites = true;

    await expect(client.send("rpc", Uint8Array.from([1]))).rejects.toMatchObject({ code: "transport" });
    expect(client.status).toBe("disconnected");
    expect(client.id).toBe(0n);
  });

  it("authenticates StopSession before deliberately forgetting session keys", async () => {
    const transport = new CryptographicDeviceTransport();
    const client = new SessionClient();
    await client.connect(transport, { id: "usb-0", label: "test", kind: "usb", metadata: {} }, {
      clientName: "field-console",
      requestedRole: 1,
      requestedCapabilities: 1,
      approve: async () => true,
    });

    await client.disconnect();

    expect(transport.stopped).toBe(true);
    expect(client.status).toBe("disconnected");
    expect(client.id).toBe(0n);
  });
});
