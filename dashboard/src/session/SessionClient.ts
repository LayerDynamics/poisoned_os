import { create, fromBinary, toBinary } from "@bufbuild/protobuf";
import type { Main } from "../generated/flipper_pb";
import { CommandStatus, MainSchema, StopSessionSchema } from "../generated/flipper_pb";
import { ChannelOpenSchema, PairingConfirmSchema, PairingHelloSchema, ResumeRequestSchema, SessionEnvelopeSchema } from "../generated/poison_session_pb";
import { ChannelState } from "./ChannelState";
import { BrowserIdentityStore, type ClientIdentityProvider } from "./BrowserIdentityStore";
import { DiscoveredDevice, Transport, TransportError, throwIfAborted, type TransportHealth, type TransportKind } from "../transports/Transport";

export type SessionStatus = "disconnected" | "connecting" | "negotiating" | "active" | "suspended" | "resuming" | "closing";

export class SessionClientError extends Error {
  public constructor(public readonly code: "state" | "protocol" | "transport" | "replay" | "cancelled", message: string) {
    super(message);
    this.name = "SessionClientError";
  }
}

export interface PairingOptions {
  readonly clientName: string;
  readonly requestedRole: number;
  readonly requestedCapabilities: number;
  readonly approve: (challenge: { confirmationCode: string; transcriptDigest: Uint8Array }) => Promise<boolean>;
}

export interface ReceivedSessionFrame {
  readonly channel: string;
  readonly sequence: bigint;
  readonly acknowledgement: bigint;
  readonly payload: Uint8Array;
}

const PROTOCOL_VERSION = 2;
const MAX_PROTOBUF_FRAME = 4096;
const MAX_SECURE_PAYLOAD = 768;
const MAX_SECURE_RESPONSE_PAYLOAD = 1280;
const MAX_SEQUENCE = 0xffffffffffffffffn;
const RESUME_TOKEN_BYTES = 32;
const encoder = new TextEncoder();

function concatenate(...parts: Uint8Array[]): Uint8Array {
  const output = new Uint8Array(parts.reduce((length, part) => length + part.byteLength, 0));
  let offset = 0;
  for (const part of parts) {
    output.set(part, offset);
    offset += part.byteLength;
  }
  return output;
}

function encodeU32(value: number): Uint8Array {
  const output = new Uint8Array(4);
  new DataView(output.buffer).setUint32(0, value, false);
  return output;
}

function encodeU64(value: bigint): Uint8Array {
  const output = new Uint8Array(8);
  new DataView(output.buffer).setBigUint64(0, value, false);
  return output;
}

function exactBuffer(value: Uint8Array): ArrayBuffer {
  return value.slice().buffer as ArrayBuffer;
}

function constantTimeEqual(left: Uint8Array, right: Uint8Array): boolean {
  if (left.byteLength !== right.byteLength) return false;
  let difference = 0;
  for (let index = 0; index < left.byteLength; index += 1) difference |= left[index] ^ right[index];
  return difference === 0;
}

function encodeDelimited(payload: Uint8Array): Uint8Array {
  const prefix: number[] = [];
  let remaining = payload.byteLength;
  do {
    let octet = remaining & 0x7f;
    remaining >>>= 7;
    if (remaining !== 0) octet |= 0x80;
    prefix.push(octet);
  } while (remaining !== 0);
  return concatenate(new Uint8Array(prefix), payload);
}

export class SessionClient {
  private statusValue: SessionStatus = "disconnected";
  private transport: Transport | null = null;
  private sessionId = 0n;
  private nextSequence = 0n;
  private nextReceiveSequence = 0n;
  private acknowledgement = 0n;
  private peerAcknowledgement: bigint | null = null;
  private nextCommandId = 1;
  private transmitKey: CryptoKey | null = null;
  private receiveKey: CryptoKey | null = null;
  private receiveBuffer: Uint8Array = new Uint8Array();
  private readAbort: AbortController | null = null;
  private readonly channels = new Map<string, ChannelState>();
  private readonly notificationHandlers = new Set<(message: Main) => void>();
  private readonly statusHandlers = new Set<(status: SessionStatus) => void>();
  private removeTransportDisconnectHandler: (() => void) | null = null;
  private requestQueue: Promise<void> = Promise.resolve();
  private requestInFlight = false;
  private resumeToken: Uint8Array | null = null;

  public constructor(private readonly identityProvider: ClientIdentityProvider = new BrowserIdentityStore()) {}

  public get status(): SessionStatus { return this.statusValue; }
  public get id(): bigint { return this.sessionId; }
  public get sequence(): bigint { return this.nextSequence; }
  public get transportKind(): TransportKind | null { return this.transport?.kind ?? null; }
  public get transportHealth(): TransportHealth | null {
    return this.transport ? { ...this.transport.health } : null;
  }

  public onNotification(handler: (message: Main) => void): () => void {
    this.notificationHandlers.add(handler);
    return () => this.notificationHandlers.delete(handler);
  }

  public onStatus(handler: (status: SessionStatus) => void): () => void {
    this.statusHandlers.add(handler);
    return () => this.statusHandlers.delete(handler);
  }

  public async connect(transport: Transport, device: DiscoveredDevice, options: PairingOptions, signal?: AbortSignal): Promise<void> {
    if (this.statusValue !== "disconnected") throw new SessionClientError("state", "session is already connected");
    const clientName = encoder.encode(options.clientName);
    if (clientName.byteLength === 0 || clientName.byteLength > 32 || options.clientName.includes("\0")) {
      throw new SessionClientError("protocol", "client name must contain 1 to 32 UTF-8 bytes");
    }
    if (!Number.isInteger(options.requestedRole) || options.requestedRole < 0 || options.requestedRole >= 5 ||
        !Number.isInteger(options.requestedCapabilities) || options.requestedCapabilities < 0 ||
        options.requestedCapabilities > 0xff) {
      throw new SessionClientError("protocol", "requested role or capabilities are invalid");
    }
    throwIfAborted(signal);
    this.setStatus("connecting");
    this.transport = transport;
    try {
      await transport.connect(device, signal);
      this.removeTransportDisconnectHandler = transport.onDisconnect?.(() => {
        void this.handleTransportDisconnect();
      }) ?? null;
      this.setStatus("negotiating");
      const ephemeral = await crypto.subtle.generateKey(
        { name: "ECDH", namedCurve: "P-256" },
        true,
        ["deriveBits"],
      );
      const clientPublic = new Uint8Array(await crypto.subtle.exportKey("raw", ephemeral.publicKey));
      const identity = await this.identityProvider.getOrCreate();
      const identityPublic = new Uint8Array(await crypto.subtle.exportKey("raw", identity.publicKey));
      const clientNonce = crypto.getRandomValues(new Uint8Array(32));
      const hello = create(PairingHelloSchema, {
        protocolVersion: PROTOCOL_VERSION,
        clientEphemeralPublicKey: clientPublic,
        clientName: options.clientName,
        requestedRole: options.requestedRole,
        requestedCapabilities: options.requestedCapabilities,
        clientNonce,
        clientIdentityPublicKey: identityPublic,
      });
      const helloCommandId = this.reserveCommandId();
      await this.writeMain(create(MainSchema, {
        commandId: helloCommandId,
        content: { case: "poisonPairingHello", value: hello },
      }), signal);
      const challengeMessage = await this.readMain(signal);
      if (challengeMessage.commandId !== helloCommandId || challengeMessage.commandStatus !== CommandStatus.OK ||
          challengeMessage.content.case !== "poisonPairingChallenge") {
        throw new SessionClientError("protocol", "device rejected the pairing hello");
      }
      const challenge = challengeMessage.content.value;
      if (challenge.protocolVersion !== PROTOCOL_VERSION || challenge.sessionId === 0n ||
          challenge.deviceEphemeralPublicKey.byteLength !== 65 || challenge.deviceEphemeralPublicKey[0] !== 0x04 ||
          challenge.deviceNonce.byteLength !== 32 || challenge.deviceNonce.every((value) => value === 0) ||
          challenge.transcriptDigest.byteLength !== 32 || !/^\d{6}$/.test(challenge.confirmationCode)) {
        throw new SessionClientError("protocol", "device returned an invalid pairing challenge");
      }
      const transcript = concatenate(
        encodeU32(PROTOCOL_VERSION),
        clientPublic,
        identityPublic,
        encoder.encode(`${options.clientName}\0`),
        encodeU32(options.requestedRole),
        encodeU32(options.requestedCapabilities),
        clientNonce,
        challenge.deviceEphemeralPublicKey,
        challenge.deviceNonce,
        encodeU64(challenge.expiresAtMs),
        encodeU64(challenge.sessionId),
      );
      const transcriptDigest = new Uint8Array(await crypto.subtle.digest("SHA-256", exactBuffer(transcript)));
      const expectedCode = String(
        new DataView(transcriptDigest.buffer, transcriptDigest.byteOffset, transcriptDigest.byteLength)
          .getUint32(0, false) % 1_000_000,
      ).padStart(6, "0");
      if (!constantTimeEqual(transcriptDigest, challenge.transcriptDigest) || expectedCode !== challenge.confirmationCode) {
        throw new SessionClientError("protocol", "pairing transcript authentication failed");
      }
      this.sessionId = challenge.sessionId;
      await this.deriveDirectionalKeys(
        ephemeral.privateKey,
        challenge.deviceEphemeralPublicKey,
        clientNonce,
        challenge.deviceNonce,
        transcriptDigest,
      );
      if (!challenge.identityTrusted &&
          !(await options.approve({ confirmationCode: challenge.confirmationCode, transcriptDigest: challenge.transcriptDigest }))) {
        throw new SessionClientError("cancelled", "physical pairing was not approved");
      }
      const confirm = create(PairingConfirmSchema, {
        transcriptDigest: challenge.transcriptDigest,
        confirmationCode: challenge.confirmationCode,
        physicalConfirmation: true,
        clientIdentitySignature: new Uint8Array(await crypto.subtle.sign(
          { name: "ECDSA", hash: "SHA-256" },
          identity.privateKey,
          exactBuffer(transcript),
        )),
      });
      const confirmCommandId = this.reserveCommandId();
      await this.writeMain(create(MainSchema, {
        commandId: confirmCommandId,
        content: { case: "poisonPairingConfirm", value: confirm },
      }), signal);
      const confirmation = await this.readMain(signal);
      if (confirmation.commandId !== confirmCommandId || confirmation.commandStatus !== CommandStatus.OK ||
          confirmation.content.case !== "empty") {
        throw new SessionClientError("protocol", "device did not physically approve pairing");
      }
      this.setStatus("active");
      await this.openRpcChannel(signal);
      await this.issueResumeToken(signal);
    } catch (error) {
      await this.close();
      if (error instanceof TransportError && error.code === "aborted") throw new SessionClientError("cancelled", error.message);
      if (error instanceof SessionClientError) throw error;
      throw new SessionClientError("transport", error instanceof Error ? error.message : String(error));
    }
  }

  public async resume(
    transport: Transport,
    device: DiscoveredDevice,
    signal?: AbortSignal,
  ): Promise<void> {
    if (this.statusValue !== "suspended" || !this.resumeToken || this.sessionId === 0n ||
        !this.transmitKey || !this.receiveKey) {
      throw new SessionClientError("state", "session is not resumable");
    }
    throwIfAborted(signal);
    await this.requestQueue;
    this.setStatus("resuming");
    this.transport = transport;
    this.receiveBuffer = new Uint8Array();
    try {
      await transport.connect(device, signal);
      this.removeTransportDisconnectHandler = transport.onDisconnect?.(() => {
        void this.handleTransportDisconnect();
      }) ?? null;
      const commandId = this.reserveCommandId();
      const lastReceivedSequence = this.nextReceiveSequence === 0n
        ? MAX_SEQUENCE
        : this.nextReceiveSequence - 1n;
      await this.writeMain(create(MainSchema, {
        commandId,
        content: {
          case: "poisonResumeRequest",
          value: create(ResumeRequestSchema, {
            sessionId: this.sessionId,
            resumeToken: this.resumeToken,
            lastReceivedSequence,
          }),
        },
      }), signal);
      const response = await this.readMain(signal);
      if (response.commandId !== commandId || response.commandStatus !== CommandStatus.OK ||
          response.content.case !== "poisonResumeResponse" || !response.content.value.accepted ||
          response.content.value.resumeToken.byteLength !== RESUME_TOKEN_BYTES ||
          response.content.value.nextSequence !== this.nextReceiveSequence) {
        const reason = response.content.case === "poisonResumeResponse"
          ? response.content.value.error
          : "invalid resume response";
        throw new SessionClientError("protocol", reason || "device refused session resumption");
      }
      this.resumeToken.fill(0);
      this.resumeToken = response.content.value.resumeToken.slice();
      this.setStatus("active");
      await this.openRpcChannel(signal);
    } catch (error) {
      await this.close();
      if (error instanceof TransportError && error.code === "aborted") {
        throw new SessionClientError("cancelled", error.message);
      }
      if (error instanceof SessionClientError) throw error;
      throw new SessionClientError("transport", error instanceof Error ? error.message : String(error));
    }
  }

  public async send(channel: string, payload: Uint8Array, signal?: AbortSignal): Promise<bigint> {
    if (this.statusValue !== "active" || !this.transport) throw new SessionClientError("state", "session is not active");
    if (channel !== "rpc") throw new SessionClientError("protocol", "firmware accepts secure PB_Main RPC only on the rpc channel");
    if (payload.byteLength === 0 || payload.byteLength > MAX_SECURE_PAYLOAD) {
      throw new SessionClientError("protocol", "secure RPC payload must contain 1 to 768 bytes");
    }
    const state = this.channels.get(channel) ?? new ChannelState(channel, 1);
    this.channels.set(channel, state);
    let sequence: bigint;
    try { sequence = state.reserveSend(payload.byteLength); } catch (error) { throw new SessionClientError("state", error instanceof Error ? error.message : String(error)); }
    if (this.nextSequence === MAX_SEQUENCE) throw new SessionClientError("replay", "session sequence exhausted");
    if (!this.transmitKey) throw new SessionClientError("protocol", "session transmit key is unavailable");
    const wireSequence = this.nextSequence;
    this.nextSequence += 1n;
    try {
      const encrypted = await this.encrypt(this.transmitKey, wireSequence, this.acknowledgement, channel, payload);
      const envelope = create(SessionEnvelopeSchema, {
        protocolVersion: PROTOCOL_VERSION,
        sessionId: this.sessionId,
        sequence: wireSequence,
        acknowledgement: this.acknowledgement,
        channel,
        payload: encrypted.slice(0, -16),
        authenticationTag: encrypted.slice(-16),
      });
      await this.writeMain(create(MainSchema, {
        commandId: this.reserveCommandId(),
        content: { case: "poisonSessionEnvelope", value: envelope },
      }), signal);
      return sequence;
    } catch (error) {
      return this.failOperation(error);
    }
  }

  public async receive(signal?: AbortSignal): Promise<ReceivedSessionFrame> {
    if (this.statusValue !== "active" || !this.receiveKey) {
      throw new SessionClientError("state", "session is not active");
    }
    try {
      return await this.receiveAuthenticated(signal);
    } catch (error) {
      return this.failOperation(error);
    }
  }

  private async receiveAuthenticated(signal?: AbortSignal): Promise<ReceivedSessionFrame> {
    if (!this.receiveKey) throw new SessionClientError("protocol", "session receive key is unavailable");
    const message = await this.readMain(signal);
    if (message.commandStatus !== CommandStatus.OK || message.content.case !== "poisonSessionEnvelope") {
      throw new SessionClientError("protocol", "device returned an unencrypted secure-session response");
    }
    const envelope = message.content.value;
    if (envelope.protocolVersion !== PROTOCOL_VERSION || envelope.sessionId !== this.sessionId ||
        envelope.sequence !== this.nextReceiveSequence || envelope.channel !== "rpc" ||
        envelope.authenticationTag.byteLength !== 16 || envelope.payload.byteLength === 0 ||
        envelope.payload.byteLength > MAX_SECURE_RESPONSE_PAYLOAD) {
      throw new SessionClientError(
        envelope.sequence < this.nextReceiveSequence ? "replay" : "protocol",
        "device returned an invalid secure-session envelope",
      );
    }
    const plaintext = await this.decrypt(
      this.receiveKey,
      envelope.sequence,
      envelope.acknowledgement,
      envelope.channel,
      concatenate(envelope.payload, envelope.authenticationTag),
    );
    if (envelope.acknowledgement >= this.nextSequence ||
        (this.peerAcknowledgement !== null && envelope.acknowledgement < this.peerAcknowledgement)) {
      throw new SessionClientError("replay", "device returned an invalid secure-session acknowledgement");
    }
    if (this.peerAcknowledgement === null || envelope.acknowledgement > this.peerAcknowledgement) {
      const released = this.peerAcknowledgement === null ? envelope.acknowledgement + 1n : envelope.acknowledgement - this.peerAcknowledgement;
      if (released > 4n) throw new SessionClientError("protocol", "device acknowledgement exceeded the credit window");
      const channel = this.channels.get(envelope.channel);
      if (!channel) throw new SessionClientError("protocol", "device acknowledged an unopened channel");
      try { channel.addCredits(Number(released)); }
      catch (error) { throw new SessionClientError("protocol", error instanceof Error ? error.message : String(error)); }
      this.peerAcknowledgement = envelope.acknowledgement;
    }
    this.nextReceiveSequence += 1n;
    this.acknowledgement = envelope.sequence;
    return {
      channel: envelope.channel,
      sequence: envelope.sequence,
      acknowledgement: envelope.acknowledgement,
      payload: plaintext,
    };
  }

  public request(request: Main, signal?: AbortSignal): Promise<Main> {
    return this.serializeRequest(() => this.requestUnlocked(request, signal));
  }

  private async requestUnlocked(request: Main, signal?: AbortSignal): Promise<Main> {
    await this.send("rpc", toBinary(MainSchema, request), signal);
    for (let notifications = 0; notifications <= 64; notifications += 1) {
      const response = await this.receive(signal);
      let message: Main;
      try {
        message = fromBinary(MainSchema, response.payload);
      } catch (error) {
        throw new SessionClientError("protocol", `device returned invalid inner PB_Main: ${error instanceof Error ? error.message : String(error)}`);
      }
      if (message.commandId === 0) {
        for (const handler of this.notificationHandlers) handler(message);
        continue;
      }
      if (message.commandId !== request.commandId) {
        throw new SessionClientError("protocol", "device returned a mismatched RPC response");
      }
      return message;
    }
    throw new SessionClientError("protocol", "device exceeded the notification bound");
  }

  public requestStream(
    request: Main,
    maxResponses = 17,
    signal?: AbortSignal,
  ): Promise<readonly Main[]> {
    return this.serializeRequest(() => this.requestStreamUnlocked(request, maxResponses, signal));
  }

  private async requestStreamUnlocked(
    request: Main,
    maxResponses: number,
    signal?: AbortSignal,
  ): Promise<readonly Main[]> {
    if (!Number.isInteger(maxResponses) || maxResponses < 1 || maxResponses > 64) {
      throw new SessionClientError("protocol", "stream response bound is invalid");
    }
    await this.send("rpc", toBinary(MainSchema, request), signal);
    const responses: Main[] = [];
    let notifications = 0;
    do {
      const frame = await this.receive(signal);
      let response: Main;
      try {
        response = fromBinary(MainSchema, frame.payload);
      } catch (error) {
        throw new SessionClientError("protocol", `device returned invalid inner PB_Main: ${error instanceof Error ? error.message : String(error)}`);
      }
      if (response.commandId === 0) {
        notifications += 1;
        if (notifications > 64) throw new SessionClientError("protocol", "device exceeded the notification bound");
        for (const handler of this.notificationHandlers) handler(response);
        continue;
      }
      if (response.commandId !== request.commandId || response.commandStatus !== CommandStatus.OK) {
        throw new SessionClientError("protocol", "device returned a mismatched streamed response");
      }
      responses.push(response);
      if (responses.length === maxResponses && response.hasNext) {
        throw new SessionClientError("protocol", "device exceeded the streamed response bound");
      }
      if (!response.hasNext) break;
    } while (responses.length < maxResponses);
    return responses;
  }

  public async cancel(signal?: AbortSignal): Promise<void> {
    await this.disconnect(signal);
  }

  public async disconnect(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (this.statusValue === "disconnected") return;
    try {
      if (this.statusValue === "active") {
        const commandId = this.reserveCommandId();
        const response = await this.request(create(MainSchema, {
          commandId,
          content: { case: "stopSession", value: create(StopSessionSchema) },
        }), signal);
        if (response.commandStatus !== CommandStatus.OK || response.content.case !== "empty") {
          throw new SessionClientError("protocol", "device did not acknowledge secure session shutdown");
        }
      }
    } finally {
      await this.close();
    }
  }

  public async close(): Promise<void> {
    if (this.statusValue === "disconnected") return;
    this.setStatus("closing");
    this.readAbort?.abort();
    this.readAbort = null;
    this.removeTransportDisconnectHandler?.();
    this.removeTransportDisconnectHandler = null;
    try {
      await this.transport?.close();
    } finally {
      this.transport = null;
      this.channels.clear();
      this.sessionId = 0n;
      this.nextSequence = 0n;
      this.nextReceiveSequence = 0n;
      this.acknowledgement = 0n;
      this.peerAcknowledgement = null;
      this.nextCommandId = 1;
      this.transmitKey = null;
      this.receiveKey = null;
      this.resumeToken?.fill(0);
      this.resumeToken = null;
      this.receiveBuffer = new Uint8Array();
      this.setStatus("disconnected");
    }
  }

  private setStatus(status: SessionStatus): void {
    if (this.statusValue === status) return;
    this.statusValue = status;
    for (const handler of this.statusHandlers) handler(status);
  }

  private async handleTransportDisconnect(): Promise<void> {
    if (this.statusValue === "disconnected" || this.statusValue === "closing") return;
    if (this.statusValue === "active" && !this.requestInFlight && this.resumeToken) {
      await this.suspend();
      return;
    }
    try { await this.close(); } catch { /* State is reset by close() even if adapter cleanup fails. */ }
  }

  private async suspend(): Promise<void> {
    if (this.statusValue !== "active") return;
    this.setStatus("suspended");
    this.readAbort?.abort();
    this.readAbort = null;
    this.removeTransportDisconnectHandler?.();
    this.removeTransportDisconnectHandler = null;
    const transport = this.transport;
    this.transport = null;
    this.receiveBuffer = new Uint8Array();
    try { await transport?.close(); } catch { /* The resume attempt uses a new adapter. */ }
  }

  private reserveCommandId(): number {
    const commandId = this.nextCommandId;
    this.nextCommandId = this.nextCommandId === 0xffffffff ? 1 : this.nextCommandId + 1;
    return commandId;
  }

  private async serializeRequest<T>(operation: () => Promise<T>): Promise<T> {
    const previous = this.requestQueue;
    let release!: () => void;
    this.requestQueue = new Promise<void>((resolve) => { release = resolve; });
    await previous;
    this.requestInFlight = true;
    try {
      return await operation();
    } catch (error) {
      return this.failOperation(error);
    } finally {
      this.requestInFlight = false;
      release();
    }
  }

  private async issueResumeToken(signal?: AbortSignal): Promise<void> {
    const commandId = this.reserveCommandId();
    const response = await this.request(create(MainSchema, {
      commandId,
      content: {
        case: "poisonResumeRequest",
        value: create(ResumeRequestSchema, {
          sessionId: this.sessionId,
          resumeToken: new Uint8Array(),
          lastReceivedSequence: 0n,
        }),
      },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonResumeResponse" ||
        !response.content.value.accepted ||
        response.content.value.resumeToken.byteLength !== RESUME_TOKEN_BYTES ||
        response.content.value.nextSequence !== this.nextReceiveSequence) {
      throw new SessionClientError("protocol", "device did not issue a valid resume token");
    }
    this.resumeToken?.fill(0);
    this.resumeToken = response.content.value.resumeToken.slice();
  }

  private async openRpcChannel(signal?: AbortSignal): Promise<void> {
    const channel = this.channels.get("rpc") ?? new ChannelState("rpc", 1);
    this.channels.set("rpc", channel);
    const resumeSequence = channel.transmitSequence;
    const commandId = this.reserveCommandId();
    const response = await this.request(create(MainSchema, {
      commandId,
      content: {
        case: "poisonChannelOpen",
        value: create(ChannelOpenSchema, {
          channel: "rpc",
          initialCredits: 4,
          resumeSequence,
        }),
      },
    }), signal);
    if (response.commandStatus !== CommandStatus.OK || response.content.case !== "poisonChannelOpened" ||
        response.content.value.channel !== "rpc" || response.content.value.nextSequence !== resumeSequence ||
        response.content.value.grantedCredits < 1 || response.content.value.grantedCredits > 4 ||
        channel.availableCredits > response.content.value.grantedCredits) {
      throw new SessionClientError("protocol", "device did not negotiate the secure rpc channel");
    }
    channel.addCredits(response.content.value.grantedCredits - channel.availableCredits);
  }

  private async failOperation(error: unknown): Promise<never> {
    const fatal = error instanceof TransportError ||
      (error instanceof SessionClientError && ["protocol", "replay", "transport"].includes(error.code));
    if (fatal && this.statusValue === "active") {
      try { await this.close(); } catch { /* Preserve the operation failure below. */ }
    }
    if (error instanceof TransportError) {
      throw new SessionClientError(error.code === "aborted" ? "cancelled" : "transport", error.message);
    }
    throw error;
  }

  private async writeMain(message: Main, signal?: AbortSignal): Promise<void> {
    if (!this.transport) throw new SessionClientError("state", "transport is unavailable");
    const framed = encodeDelimited(toBinary(MainSchema, message));
    if (framed.byteLength > this.transport.mtu) throw new SessionClientError("protocol", "encoded frame exceeds MTU");
    await this.transport.write(framed, signal);
  }

  private async deriveDirectionalKeys(
    clientPrivateKey: CryptoKey,
    devicePublicBytes: Uint8Array,
    clientNonce: Uint8Array,
    deviceNonce: Uint8Array,
    transcriptDigest: Uint8Array,
  ): Promise<void> {
    try {
      const devicePublicKey = await crypto.subtle.importKey(
        "raw",
        exactBuffer(devicePublicBytes),
        { name: "ECDH", namedCurve: "P-256" },
        false,
        [],
      );
      const sharedSecret = await crypto.subtle.deriveBits(
        { name: "ECDH", public: devicePublicKey },
        clientPrivateKey,
        256,
      );
      const hkdfKey = await crypto.subtle.importKey("raw", sharedSecret, "HKDF", false, ["deriveBits"]);
      const material = new Uint8Array(await crypto.subtle.deriveBits({
        name: "HKDF",
        hash: "SHA-256",
        salt: exactBuffer(concatenate(clientNonce, deviceNonce)),
        info: exactBuffer(concatenate(encoder.encode("poison-rpc-v2"), transcriptDigest)),
      }, hkdfKey, 512));
      this.transmitKey = await crypto.subtle.importKey(
        "raw",
        exactBuffer(material.slice(0, 32)),
        "AES-GCM",
        false,
        ["encrypt"],
      );
      this.receiveKey = await crypto.subtle.importKey(
        "raw",
        exactBuffer(material.slice(32)),
        "AES-GCM",
        false,
        ["decrypt"],
      );
      material.fill(0);
    } catch (error) {
      throw new SessionClientError("protocol", `pairing key derivation failed: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  private sessionAad(
    sequence: bigint,
    acknowledgement: bigint,
    channel: string,
  ): Uint8Array {
    return concatenate(
      encodeU32(PROTOCOL_VERSION),
      encodeU64(this.sessionId),
      encodeU64(sequence),
      encodeU64(acknowledgement),
      encoder.encode(`${channel}\0`),
    );
  }

  private sessionIv(sequence: bigint): Uint8Array {
    return concatenate(encodeU32(Number(this.sessionId >> 32n)), encodeU64(sequence));
  }

  private async encrypt(
    key: CryptoKey,
    sequence: bigint,
    acknowledgement: bigint,
    channel: string,
    plaintext: Uint8Array,
  ): Promise<Uint8Array> {
    try {
      return new Uint8Array(await crypto.subtle.encrypt({
        name: "AES-GCM",
        iv: exactBuffer(this.sessionIv(sequence)),
        additionalData: exactBuffer(this.sessionAad(sequence, acknowledgement, channel)),
        tagLength: 128,
      }, key, exactBuffer(plaintext)));
    } catch (error) {
      throw new SessionClientError("protocol", `session encryption failed: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  private async decrypt(
    key: CryptoKey,
    sequence: bigint,
    acknowledgement: bigint,
    channel: string,
    ciphertextAndTag: Uint8Array,
  ): Promise<Uint8Array> {
    try {
      return new Uint8Array(await crypto.subtle.decrypt({
        name: "AES-GCM",
        iv: exactBuffer(this.sessionIv(sequence)),
        additionalData: exactBuffer(this.sessionAad(sequence, acknowledgement, channel)),
        tagLength: 128,
      }, key, exactBuffer(ciphertextAndTag)));
    } catch (error) {
      throw new SessionClientError("protocol", `session response authentication failed: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  private async readMain(signal?: AbortSignal): Promise<Main> {
    const payload = await this.readDelimited(signal);
    try {
      return fromBinary(MainSchema, payload);
    } catch (error) {
      throw new SessionClientError("protocol", `device returned invalid PB_Main: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  private async readDelimited(signal?: AbortSignal): Promise<Uint8Array> {
    if (!this.transport) throw new SessionClientError("state", "transport is unavailable");
    while (true) {
      let length = 0;
      let shift = 0;
      let prefixLength = 0;
      let completePrefix = false;
      for (; prefixLength < this.receiveBuffer.byteLength && prefixLength < 5; prefixLength += 1) {
        const octet = this.receiveBuffer[prefixLength];
        length |= (octet & 0x7f) << shift;
        if ((octet & 0x80) === 0) {
          prefixLength += 1;
          completePrefix = true;
          break;
        }
        shift += 7;
      }
      if (completePrefix) {
        if (length <= 0 || length > MAX_PROTOBUF_FRAME) {
          throw new SessionClientError("protocol", "device returned an invalid protobuf frame length");
        }
        if (this.receiveBuffer.byteLength >= prefixLength + length) {
          const payload = this.receiveBuffer.slice(prefixLength, prefixLength + length);
          this.receiveBuffer = this.receiveBuffer.slice(prefixLength + length);
          return payload;
        }
      } else if (prefixLength === 5) {
        throw new SessionClientError("protocol", "device returned an invalid protobuf length prefix");
      }

      this.readAbort = new AbortController();
      const onAbort = () => this.readAbort?.abort();
      signal?.addEventListener("abort", onAbort, { once: true });
      let chunk: Uint8Array | null;
      try {
        chunk = await this.transport.read(this.readAbort.signal);
      } finally {
        signal?.removeEventListener("abort", onAbort);
        this.readAbort = null;
      }
      if (chunk === null) throw new SessionClientError("protocol", "device closed during protobuf frame");
      if (chunk.byteLength !== 0) this.receiveBuffer = concatenate(this.receiveBuffer, chunk);
    }
  }
}
