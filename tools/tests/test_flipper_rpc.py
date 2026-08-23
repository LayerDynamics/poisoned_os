from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
RPC_MODULE = ROOT / "scripts" / "flipper" / "rpc.py"
PROTO_DIR = ROOT / "generated" / "protocol" / "python"
sys.path.insert(0, str(PROTO_DIR))

import flipper_pb2


def load_rpc_module():
    spec = importlib.util.spec_from_file_location("flipper_rpc_under_test", RPC_MODULE)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FragmentedStream:
    def __init__(self, incoming: bytes = b"") -> None:
        self.incoming = bytearray(incoming)
        self.writes: list[bytes] = []

    def read(self, size: int) -> bytes:
        if not self.incoming:
            return b""
        size = min(size, 1)
        data = bytes(self.incoming[:size])
        del self.incoming[:size]
        return data

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        return len(data)

    def flush(self) -> None:
        return None


class SessionStream(FragmentedStream):
    def __init__(self, module) -> None:
        super().__init__()
        self.module = module
        self.closed = False
        self.reset_count = 0
        self._dtr = True
        self.dtr_transitions: list[bool] = []

    @property
    def dtr(self) -> bool:
        return self._dtr

    @dtr.setter
    def dtr(self, active: bool) -> None:
        self._dtr = active
        self.dtr_transitions.append(active)
        if active:
            self.incoming.extend(b"Flipper CLI\r\n>: ")
        else:
            self.incoming.clear()

    def reset_input_buffer(self) -> None:
        self.reset_count += 1
        self.incoming.clear()

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        if data == b"start_rpc_session\r":
            self.incoming.extend(b"start_rpc_session\r\n")
        else:
            request = self.module.decode_delimited(data)
            if request.HasField("system_ping_request"):
                response = flipper_pb2.Main(
                    command_id=request.command_id,
                    command_status=flipper_pb2.OK,
                )
                response.system_ping_response.data = request.system_ping_request.data
                self.incoming.extend(self.module.encode_delimited(response))
        return len(data)

    def close(self) -> None:
        self.closed = True


class FlipperRpcTests(unittest.TestCase):
    def test_start_session_reports_silent_rpc_handoff(self) -> None:
        module = load_rpc_module()
        stream = SessionStream(module)

        def silent_write(data: bytes) -> int:
            stream.writes.append(bytes(data))
            return len(data)

        stream.write = silent_write
        client = module.FlipperRpc(
            "/dev/cu.fixture",
            serial_factory=lambda *_args, **_kwargs: stream,
            timeout=0.01,
        )

        with mock.patch.object(module.time, "sleep"):
            with self.assertRaisesRegex(
                module.RpcError,
                "timed out waiting for Flipper CLI marker.*start_rpc_session",
            ):
                client.start()

        self.assertTrue(stream.closed)

    def test_start_session_matches_qflipper_dtr_and_cli_handoff(self) -> None:
        module = load_rpc_module()
        stream = SessionStream(module)
        client = module.FlipperRpc(
            "/dev/cu.fixture",
            serial_factory=lambda *_args, **_kwargs: stream,
            timeout=0.01,
        )

        with mock.patch.object(module.time, "sleep"):
            client.start()

        self.assertEqual(stream.dtr_transitions, [False, True])
        self.assertEqual(stream.writes[0], b"start_rpc_session\r")
        self.assertEqual(stream.reset_count, 0)
        ping = module.decode_delimited(stream.writes[1])
        self.assertTrue(ping.HasField("system_ping_request"))
        client.close()

    def test_delimited_codec_handles_fragmented_multibyte_length(self) -> None:
        module = load_rpc_module()
        request = flipper_pb2.Main(command_id=19)
        request.system_ping_request.data = b"x" * 200
        framed = module.encode_delimited(request)

        decoded = module.read_delimited(FragmentedStream(framed), timeout=0.2)

        self.assertEqual(decoded, request)
        self.assertGreaterEqual(framed[0] & 0x80, 0x80)

    def test_write_file_streams_512_byte_chunks_under_one_command(self) -> None:
        module = load_rpc_module()
        response = flipper_pb2.Main(command_id=1, command_status=flipper_pb2.OK)
        stream = FragmentedStream(module.encode_delimited(response))
        client = module.FlipperRpc("fixture", stream=stream)

        with tempfile.TemporaryDirectory() as temporary:
            local = Path(temporary) / "firmware.dfu"
            local.write_bytes(bytes(range(256)) * 5)
            client.write_file(local, "/ext/update/poison/firmware.dfu")

        requests = [module.decode_delimited(frame) for frame in stream.writes]
        self.assertEqual([message.command_id for message in requests], [1, 1, 1])
        self.assertEqual([message.has_next for message in requests], [True, True, False])
        self.assertEqual(
            [len(message.storage_write_request.file.data) for message in requests],
            [512, 512, 256],
        )
        self.assertEqual(
            {message.storage_write_request.path for message in requests},
            {"/ext/update/poison/firmware.dfu"},
        )

    def test_device_info_collects_all_streamed_responses(self) -> None:
        module = load_rpc_module()
        first = flipper_pb2.Main(
            command_id=1,
            command_status=flipper_pb2.OK,
            has_next=True,
        )
        first.system_device_info_response.key = "hardware_model"
        first.system_device_info_response.value = "Flipper Zero"
        second = flipper_pb2.Main(command_id=1, command_status=flipper_pb2.OK)
        second.system_device_info_response.key = "firmware_origin_fork"
        second.system_device_info_response.value = "PoisonedOS"
        stream = FragmentedStream(
            module.encode_delimited(first) + module.encode_delimited(second)
        )
        client = module.FlipperRpc("fixture", stream=stream)

        self.assertEqual(
            client.device_info(),
            {
                "hardware_model": "Flipper Zero",
                "firmware_origin_fork": "PoisonedOS",
            },
        )

    def test_read_file_collects_chunks_and_reports_missing_file(self) -> None:
        module = load_rpc_module()
        first = flipper_pb2.Main(
            command_id=1,
            command_status=flipper_pb2.OK,
            has_next=True,
        )
        first.storage_read_response.file.data = b"run"
        second = flipper_pb2.Main(command_id=1, command_status=flipper_pb2.OK)
        second.storage_read_response.file.data = b"ning\n"
        stream = FragmentedStream(
            module.encode_delimited(first) + module.encode_delimited(second)
        )
        self.assertEqual(
            module.FlipperRpc("fixture", stream=stream).read_file("/ext/status"),
            b"running\n",
        )

        missing = flipper_pb2.Main(
            command_id=1,
            command_status=flipper_pb2.ERROR_STORAGE_NOT_EXIST,
        )
        missing_stream = FragmentedStream(module.encode_delimited(missing))
        self.assertIsNone(
            module.FlipperRpc("fixture", stream=missing_stream).read_file("/ext/status")
        )

    def test_start_app_uses_real_application_request(self) -> None:
        module = load_rpc_module()
        response = flipper_pb2.Main(command_id=1, command_status=flipper_pb2.OK)
        stream = FragmentedStream(module.encode_delimited(response))
        client = module.FlipperRpc("fixture", stream=stream)

        client.start_app("/ext/apps/GPIO/flasher.fap", "marauder_flipper")

        request = module.decode_delimited(stream.writes[0])
        self.assertEqual(request.app_start_request.name, "/ext/apps/GPIO/flasher.fap")
        self.assertEqual(request.app_start_request.args, "marauder_flipper")

    def test_update_preparation_is_followed_by_reboot_to_updater(self) -> None:
        module = load_rpc_module()
        prepared = flipper_pb2.Main(command_id=1, command_status=flipper_pb2.OK)
        prepared.system_update_response.code = 0
        stream = FragmentedStream(module.encode_delimited(prepared))
        client = module.FlipperRpc("fixture", stream=stream)

        client.start_update("/ext/update/poison/update.fuf")

        prepare = module.decode_delimited(stream.writes[0])
        reboot = module.decode_delimited(stream.writes[1])
        self.assertEqual(
            prepare.system_update_request.update_manifest,
            "/ext/update/poison/update.fuf",
        )
        self.assertEqual(reboot.system_reboot_request.mode, 2)


if __name__ == "__main__":
    unittest.main()
