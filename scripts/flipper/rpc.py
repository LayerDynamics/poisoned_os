from __future__ import annotations

from pathlib import Path
import sys
import time
from typing import BinaryIO, Callable


ROOT = Path(__file__).resolve().parents[2]
GENERATED_PROTOCOL = ROOT / "generated" / "protocol" / "python"
if str(GENERATED_PROTOCOL) not in sys.path:
    sys.path.insert(0, str(GENERATED_PROTOCOL))

import flipper_pb2
import application_pb2
import storage_pb2
import system_pb2


MAX_MESSAGE_BYTES = 16_384
STORAGE_CHUNK_BYTES = 512
RPC_TIMEOUT = 10.0
SERIAL_BAUD = 230_400


class RpcError(RuntimeError):
    pass


def _encode_varint(value: int) -> bytes:
    if value < 0:
        raise ValueError("varint cannot encode a negative value")
    encoded = bytearray()
    while value > 0x7F:
        encoded.append((value & 0x7F) | 0x80)
        value >>= 7
    encoded.append(value)
    return bytes(encoded)


def encode_delimited(message: flipper_pb2.Main) -> bytes:
    payload = message.SerializeToString()
    if len(payload) > MAX_MESSAGE_BYTES:
        raise RpcError(
            f"RPC message is {len(payload)} bytes; maximum is {MAX_MESSAGE_BYTES}"
        )
    return _encode_varint(len(payload)) + payload


def _read_exact(stream: BinaryIO, size: int, deadline: float) -> bytes:
    result = bytearray()
    while len(result) < size:
        chunk = stream.read(size - len(result))
        if chunk:
            result.extend(chunk)
            continue
        if time.monotonic() >= deadline:
            raise RpcError(
                f"timed out reading RPC payload ({len(result)}/{size} bytes)"
            )
    return bytes(result)


def read_delimited(stream: BinaryIO, timeout: float = RPC_TIMEOUT) -> flipper_pb2.Main:
    deadline = time.monotonic() + timeout
    size = 0
    shift = 0
    while shift < 35:
        raw = _read_exact(stream, 1, deadline)[0]
        size |= (raw & 0x7F) << shift
        if not raw & 0x80:
            break
        shift += 7
    else:
        raise RpcError("invalid RPC length varint")
    if size > MAX_MESSAGE_BYTES:
        raise RpcError(f"RPC frame length {size} exceeds {MAX_MESSAGE_BYTES}")
    message = flipper_pb2.Main()
    try:
        message.ParseFromString(_read_exact(stream, size, deadline))
    except Exception as error:
        raise RpcError(f"invalid RPC protobuf payload: {error}") from error
    return message


def decode_delimited(frame: bytes) -> flipper_pb2.Main:
    from io import BytesIO

    stream = BytesIO(frame)
    message = read_delimited(stream)
    if stream.read(1):
        raise RpcError("delimited frame contains trailing bytes")
    return message


class FlipperRpc:
    def __init__(
        self,
        port: str,
        *,
        stream: BinaryIO | None = None,
        serial_factory: Callable[..., BinaryIO] | None = None,
        timeout: float = RPC_TIMEOUT,
    ) -> None:
        self.port = port
        self.stream = stream
        self.serial_factory = serial_factory
        self.timeout = timeout
        self._next_command_id = 1
        self._owns_stream = stream is None
        self._started = not self._owns_stream

    def __enter__(self) -> "FlipperRpc":
        self.start()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()

    def _read_until(self, marker: bytes, timeout: float) -> bytes:
        if self.stream is None:
            raise RpcError("RPC serial stream is not open")
        deadline = time.monotonic() + timeout
        received = bytearray()
        while marker not in received:
            chunk = self.stream.read(1)
            if chunk:
                received.extend(chunk)
                if len(received) > 4096:
                    del received[:-2048]
            elif time.monotonic() >= deadline:
                if not received and marker == b">: ":
                    raise RpcError(
                        "Flipper runtime is present but its CLI is not responding; "
                        "unlock a PIN-locked Flipper and retry"
                    )
                raise RpcError(
                    f"timed out waiting for Flipper CLI marker {marker!r}; "
                    f"received {bytes(received)!r}"
                )
        return bytes(received)

    def start(self) -> None:
        if self._started:
            return
        if self.serial_factory is None:
            import serial

            self.serial_factory = serial.Serial
        try:
            self.stream = self.serial_factory(
                self.port,
                SERIAL_BAUD,
                timeout=0.1,
                write_timeout=self.timeout,
            )
            if self.stream.dtr:
                self.stream.dtr = False
                time.sleep(0.05)
            self.stream.dtr = True
            self._read_until(b">: ", self.timeout)
            self.stream.write(b"start_rpc_session\r")
            self.stream.flush()
            self._read_until(b"start_rpc_session\r\n", self.timeout)
            self._started = True
            challenge = b"poisoned-os-rpc"
            if self.ping(challenge) != challenge:
                raise RpcError("Flipper RPC ping returned the wrong payload")
        except Exception:
            if self.stream is not None:
                self.stream.close()
            self.stream = None
            self._started = False
            raise

    def close(self) -> None:
        if self.stream is None:
            return
        if self._started:
            try:
                command_id = self._allocate_command_id()
                self._write_message(
                    flipper_pb2.Main(
                        command_id=command_id,
                        stop_session=flipper_pb2.StopSession(),
                    )
                )
                self.stream.flush()
            except Exception:
                pass
        if self._owns_stream:
            self.stream.close()
            self.stream = None
            self._started = False

    def _allocate_command_id(self) -> int:
        command_id = self._next_command_id
        self._next_command_id = 1 if command_id == 0xFFFFFFFF else command_id + 1
        return command_id

    def _write_message(self, message: flipper_pb2.Main) -> None:
        if self.stream is None:
            raise RpcError("RPC serial stream is not open")
        frame = encode_delimited(message)
        written = self.stream.write(frame)
        if written is not None and written != len(frame):
            raise RpcError(f"short RPC write ({written}/{len(frame)} bytes)")

    def _read_responses(
        self,
        command_id: int,
        allowed_statuses: tuple[int, ...] = (flipper_pb2.OK,),
    ) -> list[flipper_pb2.Main]:
        if self.stream is None:
            raise RpcError("RPC serial stream is not open")
        responses = []
        while True:
            response = read_delimited(self.stream, self.timeout)
            if response.command_id != command_id:
                raise RpcError(
                    f"RPC response command {response.command_id} does not match {command_id}"
                )
            if response.command_status not in allowed_statuses:
                status = flipper_pb2.CommandStatus.Name(response.command_status)
                raise RpcError(f"Flipper RPC command {command_id} failed: {status}")
            responses.append(response)
            if not response.has_next:
                return responses

    def _request(
        self,
        message: flipper_pb2.Main,
        allowed_statuses: tuple[int, ...] = (flipper_pb2.OK,),
    ) -> list[flipper_pb2.Main]:
        message.command_id = self._allocate_command_id()
        self._write_message(message)
        if self.stream is None:
            raise RpcError("RPC serial stream is not open")
        self.stream.flush()
        return self._read_responses(message.command_id, allowed_statuses)

    def ping(self, data: bytes) -> bytes:
        responses = self._request(
            flipper_pb2.Main(system_ping_request=system_pb2.PingRequest(data=data))
        )
        if len(responses) != 1 or not responses[0].HasField("system_ping_response"):
            raise RpcError("Flipper RPC ping returned an invalid response")
        return responses[0].system_ping_response.data

    def protobuf_version(self) -> tuple[int, int]:
        responses = self._request(
            flipper_pb2.Main(
                system_protobuf_version_request=system_pb2.ProtobufVersionRequest()
            )
        )
        if len(responses) != 1 or not responses[0].HasField(
            "system_protobuf_version_response"
        ):
            raise RpcError("Flipper RPC returned no protobuf version")
        version = responses[0].system_protobuf_version_response
        return version.major, version.minor

    def device_info(self) -> dict[str, str]:
        responses = self._request(
            flipper_pb2.Main(system_device_info_request=system_pb2.DeviceInfoRequest())
        )
        properties: dict[str, str] = {}
        for response in responses:
            if not response.HasField("system_device_info_response"):
                raise RpcError("Flipper RPC returned invalid device-info content")
            item = response.system_device_info_response
            properties[item.key] = item.value
        return properties

    @staticmethod
    def _storage_file_info(file_info: storage_pb2.File) -> dict[str, int | str]:
        return {
            "type": "directory" if file_info.type == storage_pb2.File.DIR else "file",
            "name": file_info.name,
            "size": file_info.size,
        }

    def storage_info(self, remote_path: str) -> dict[str, int]:
        responses = self._request(
            flipper_pb2.Main(
                storage_info_request=storage_pb2.InfoRequest(path=remote_path)
            )
        )
        if len(responses) != 1 or not responses[0].HasField("storage_info_response"):
            raise RpcError("Flipper RPC returned invalid storage-info content")
        info = responses[0].storage_info_response
        return {"total_space": info.total_space, "free_space": info.free_space}

    def stat(self, remote_path: str) -> dict[str, int | str] | None:
        responses = self._request(
            flipper_pb2.Main(
                storage_stat_request=storage_pb2.StatRequest(path=remote_path)
            ),
            (flipper_pb2.OK, flipper_pb2.ERROR_STORAGE_NOT_EXIST),
        )
        if responses[0].command_status == flipper_pb2.ERROR_STORAGE_NOT_EXIST:
            return None
        if len(responses) != 1 or not responses[0].HasField("storage_stat_response"):
            raise RpcError("Flipper RPC returned invalid storage-stat content")
        response = responses[0].storage_stat_response
        if not response.HasField("file"):
            raise RpcError("Flipper RPC storage-stat response has no file")
        return self._storage_file_info(response.file)

    def list_dir(self, remote_path: str) -> list[dict[str, int | str]]:
        responses = self._request(
            flipper_pb2.Main(
                storage_list_request=storage_pb2.ListRequest(path=remote_path)
            ),
            (flipper_pb2.OK, flipper_pb2.ERROR_STORAGE_NOT_EXIST),
        )
        if responses[0].command_status == flipper_pb2.ERROR_STORAGE_NOT_EXIST:
            return []
        entries = []
        for response in responses:
            if not response.HasField("storage_list_response"):
                raise RpcError("Flipper RPC returned invalid storage-list content")
            entries.extend(
                self._storage_file_info(file_info)
                for file_info in response.storage_list_response.file
            )
        return entries

    def delete_tree(self, remote_path: str) -> None:
        self._request(
            flipper_pb2.Main(
                storage_delete_request=storage_pb2.DeleteRequest(
                    path=remote_path, recursive=True
                )
            ),
            (flipper_pb2.OK, flipper_pb2.ERROR_STORAGE_NOT_EXIST),
        )

    def make_dir(self, remote_path: str) -> None:
        self._request(
            flipper_pb2.Main(
                storage_mkdir_request=storage_pb2.MkdirRequest(path=remote_path)
            ),
            (flipper_pb2.OK, flipper_pb2.ERROR_STORAGE_EXIST),
        )

    def make_dirs(self, remote_path: str) -> None:
        if not remote_path.startswith("/"):
            raise RpcError(f"remote path must be absolute: {remote_path}")
        current = ""
        for component in remote_path.split("/"):
            if not component:
                continue
            current += f"/{component}"
            self.make_dir(current)

    def write_file(self, local_path: Path, remote_path: str) -> None:
        command_id = self._allocate_command_id()
        size = local_path.stat().st_size
        sent = 0
        with local_path.open("rb") as source:
            while True:
                data = source.read(STORAGE_CHUNK_BYTES)
                sent += len(data)
                final = sent == size
                self._write_message(
                    flipper_pb2.Main(
                        command_id=command_id,
                        has_next=not final,
                        storage_write_request=storage_pb2.WriteRequest(
                            path=remote_path,
                            file=storage_pb2.File(data=data),
                        ),
                    )
                )
                if final:
                    break
        if self.stream is None:
            raise RpcError("RPC serial stream is not open")
        self.stream.flush()
        self._read_responses(command_id)

    def upload_file(self, local_path: Path, remote_path: str) -> None:
        local_path = Path(local_path)
        if not local_path.is_file():
            raise RpcError(f"local upload file does not exist: {local_path}")
        parent = remote_path.rsplit("/", 1)[0]
        if parent:
            self.make_dirs(parent)
        self.write_file(local_path, remote_path)

    def read_file(self, remote_path: str) -> bytes | None:
        responses = self._request(
            flipper_pb2.Main(
                storage_read_request=storage_pb2.ReadRequest(path=remote_path)
            ),
            (flipper_pb2.OK, flipper_pb2.ERROR_STORAGE_NOT_EXIST),
        )
        if responses[0].command_status == flipper_pb2.ERROR_STORAGE_NOT_EXIST:
            return None
        data = bytearray()
        for response in responses:
            if not response.HasField("storage_read_response"):
                raise RpcError("Flipper RPC returned invalid storage-read content")
            data.extend(response.storage_read_response.file.data)
        return bytes(data)

    def upload_tree(self, local_root: Path, remote_root: str) -> None:
        if not local_root.is_dir():
            raise RpcError(f"update package directory does not exist: {local_root}")
        self.delete_tree(remote_root)
        self.make_dir("/ext/update")
        self.make_dir(remote_root)
        for local_path in sorted(local_root.rglob("*")):
            relative = local_path.relative_to(local_root).as_posix()
            remote_path = f"{remote_root}/{relative}"
            if local_path.is_dir():
                self.make_dir(remote_path)
            elif local_path.is_file():
                self.write_file(local_path, remote_path)

    def start_app(self, app_path: str, args: str = "") -> None:
        responses = self._request(
            flipper_pb2.Main(
                app_start_request=application_pb2.StartRequest(
                    name=app_path,
                    args=args,
                )
            )
        )
        if len(responses) != 1 or responses[0].WhichOneof("content") is not None:
            raise RpcError("Flipper RPC returned invalid application-start content")

    def start_update(self, remote_manifest: str) -> None:
        responses = self._request(
            flipper_pb2.Main(
                system_update_request=system_pb2.UpdateRequest(
                    update_manifest=remote_manifest
                )
            )
        )
        if len(responses) != 1 or not responses[0].HasField("system_update_response"):
            raise RpcError("Flipper RPC returned no update result")
        code = responses[0].system_update_response.code
        if code != system_pb2.UpdateResponse.OK:
            name = system_pb2.UpdateResponse.UpdateResultCode.Name(code)
            raise RpcError(f"Flipper rejected update manifest: {name}")
        command_id = self._allocate_command_id()
        self._write_message(
            flipper_pb2.Main(
                command_id=command_id,
                system_reboot_request=system_pb2.RebootRequest(
                    mode=system_pb2.RebootRequest.UPDATE
                ),
            )
        )
        if self.stream is None:
            raise RpcError("RPC serial stream is not open")
        self.stream.flush()
        self._started = False
