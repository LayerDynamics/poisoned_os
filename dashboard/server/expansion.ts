export const EXPANSION_DEFAULT_BAUD_RATE = 9_600;
export const EXPANSION_MAX_DATA_SIZE = 64;
export const EXPANSION_TIMEOUT_MS = 250;
export const EXPANSION_BAUD_CHANGE_DELAY_MS = 25;

export enum ExpansionFrameType {
  Heartbeat = 0x01,
  Status = 0x02,
  BaudRate = 0x03,
  Control = 0x04,
  Data = 0x05,
}

export enum ExpansionControlCommand {
  StartRpc = 0x00,
  StopRpc = 0x01,
  EnableOtg = 0x02,
  DisableOtg = 0x03,
}

export type ExpansionFrame =
  | { type: ExpansionFrameType.Heartbeat }
  | { type: ExpansionFrameType.Status; error: number }
  | { type: ExpansionFrameType.BaudRate; baud: number }
  | { type: ExpansionFrameType.Control; command: number }
  | { type: ExpansionFrameType.Data; data: Uint8Array };

function checksum(bytes: Uint8Array): number {
  let result = 0;
  for (const byte of bytes) {
    result ^= byte;
  }
  return result;
}

function encode(body: Uint8Array): Uint8Array {
  const encoded = new Uint8Array(body.byteLength + 1);
  encoded.set(body);
  encoded[body.byteLength] = checksum(body);
  return encoded;
}

function requireByte(value: number, label: string): void {
  if (!Number.isInteger(value) || value < 0 || value > 0xff) {
    throw new RangeError(`${label} must be an unsigned byte`);
  }
}

export function encodeHeartbeat(): Uint8Array {
  return encode(Uint8Array.of(ExpansionFrameType.Heartbeat));
}

export function encodeStatus(error: number): Uint8Array {
  requireByte(error, "Expansion status error");
  return encode(Uint8Array.of(ExpansionFrameType.Status, error));
}

export function encodeBaudRate(baud: number): Uint8Array {
  if (!Number.isInteger(baud) || baud <= 0 || baud > 0xffff_ffff) {
    throw new RangeError("Expansion baud rate must be an unsigned 32-bit integer");
  }

  const body = new Uint8Array(5);
  body[0] = ExpansionFrameType.BaudRate;
  new DataView(body.buffer).setUint32(1, baud, true);
  return encode(body);
}

export function encodeControl(command: number): Uint8Array {
  requireByte(command, "Expansion control command");
  return encode(Uint8Array.of(ExpansionFrameType.Control, command));
}

export function encodeData(data: Uint8Array): Uint8Array {
  if (data.byteLength > EXPANSION_MAX_DATA_SIZE) {
    throw new RangeError(`Expansion data frames cannot exceed ${EXPANSION_MAX_DATA_SIZE} bytes`);
  }

  const body = new Uint8Array(data.byteLength + 2);
  body[0] = ExpansionFrameType.Data;
  body[1] = data.byteLength;
  body.set(data, 2);
  return encode(body);
}

function isFrameType(value: number): value is ExpansionFrameType {
  return value >= ExpansionFrameType.Heartbeat && value <= ExpansionFrameType.Data;
}

function bodyLength(buffer: Uint8Array): number | undefined {
  const type = buffer[0];
  if (!isFrameType(type)) {
    throw new Error(`Unknown expansion frame type 0x${type.toString(16).padStart(2, "0")}`);
  }

  switch (type) {
    case ExpansionFrameType.Heartbeat:
      return 1;
    case ExpansionFrameType.Status:
    case ExpansionFrameType.Control:
      return 2;
    case ExpansionFrameType.BaudRate:
      return 5;
    case ExpansionFrameType.Data: {
      if (buffer.byteLength < 2) {
        return undefined;
      }
      const size = buffer[1];
      if (size > EXPANSION_MAX_DATA_SIZE) {
        throw new Error(`Expansion data frames cannot exceed ${EXPANSION_MAX_DATA_SIZE} bytes`);
      }
      return size + 2;
    }
  }
}

function decodeBody(body: Uint8Array): ExpansionFrame {
  const type = body[0] as ExpansionFrameType;
  switch (type) {
    case ExpansionFrameType.Heartbeat:
      return { type };
    case ExpansionFrameType.Status:
      return { type, error: body[1] };
    case ExpansionFrameType.BaudRate:
      return { type, baud: new DataView(body.buffer, body.byteOffset, body.byteLength).getUint32(1, true) };
    case ExpansionFrameType.Control:
      return { type, command: body[1] };
    case ExpansionFrameType.Data:
      return { type, data: body.slice(2) };
    default:
      throw new Error(`Unknown expansion frame type ${type}`);
  }
}

export class ExpansionDecoder {
  private buffer = new Uint8Array(0);

  push(chunk: Uint8Array): ExpansionFrame[] {
    if (chunk.byteLength > 0) {
      const joined = new Uint8Array(this.buffer.byteLength + chunk.byteLength);
      joined.set(this.buffer);
      joined.set(chunk, this.buffer.byteLength);
      this.buffer = joined;
    }

    const frames: ExpansionFrame[] = [];
    while (this.buffer.byteLength > 0) {
      const expectedBodyLength = bodyLength(this.buffer);
      if (expectedBodyLength === undefined || this.buffer.byteLength < expectedBodyLength + 1) {
        break;
      }

      const body = this.buffer.subarray(0, expectedBodyLength);
      const receivedChecksum = this.buffer[expectedBodyLength];
      const expectedChecksum = checksum(body);
      if (receivedChecksum !== expectedChecksum) {
        throw new Error(
          `Expansion frame checksum mismatch: received 0x${receivedChecksum.toString(16)}, expected 0x${expectedChecksum.toString(16)}`,
        );
      }

      frames.push(decodeBody(body));
      this.buffer = this.buffer.slice(expectedBodyLength + 1);
    }
    return frames;
  }
}
