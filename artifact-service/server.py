#!/usr/bin/env python3
"""Railway service that builds and serves versioned Poisoned_Os artifacts."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import threading
import time
from urllib.parse import unquote


ROOT = Path(os.environ.get("ARTIFACT_ROOT", "/src")).resolve()
OUTPUT = Path(os.environ.get("ARTIFACT_OUTPUT", "/srv")).resolve()
PORT = int(os.environ.get("PORT", "8080"))
TOKEN = os.environ.get("ARTIFACT_BUILD_TOKEN", "")
VERSION = re.compile(r"^\d+\.\d+\.\d+$")
CHANNELS = {"stable", "beta", "developer", "internal"}
BUILD_LOCK = threading.Lock()
STATE_LOCK = threading.Lock()
STATE = {"status": "idle", "version": None, "channel": None, "error": None, "updatedAt": time.time()}


def _set_state(**values: object) -> None:
    with STATE_LOCK:
        STATE.update(values, updatedAt=time.time())


def _build(version: str, channel: str) -> None:
    if not BUILD_LOCK.acquire(blocking=False):
        raise RuntimeError("an artifact build is already running")
    try:
        _set_state(status="building", version=version, channel=channel, error=None)
        environment = os.environ.copy()
        environment["POISON_RELEASE_PRIVATE_KEY_B64"] = os.environ.get("POISON_RELEASE_PRIVATE_KEY_B64", "")
        OUTPUT.mkdir(parents=True, exist_ok=True)
        staging = Path(tempfile.mkdtemp(prefix=".poisoned-artifacts-", dir=OUTPUT))
        subprocess.run(
            [
                "uv", "run", "--no-project", "--python", "3.11", "python",
                "tools/release/build_release_artifacts.py",
                "--root", str(ROOT), "--output", str(staging), "--version", version,
                "--channel", channel, "--key-id", os.environ.get("POISON_RELEASE_KEY_ID", ""),
            ],
            cwd=ROOT,
            env=environment,
            check=True,
            timeout=45 * 60,
        )
        for existing in OUTPUT.iterdir():
            if existing != staging:
                if existing.is_dir():
                    shutil.rmtree(existing)
                else:
                    existing.unlink()
        for artifact in staging.iterdir():
            os.replace(artifact, OUTPUT / artifact.name)
        staging.rmdir()
        _set_state(status="ready", version=version, channel=channel, error=None)
    except Exception as error:
        _set_state(status="error", error=str(error))
        raise
    finally:
        if "staging" in locals() and staging.exists():
            shutil.rmtree(staging, ignore_errors=True)
        BUILD_LOCK.release()


def _start_build(version: str, channel: str) -> None:
    if not VERSION.fullmatch(version):
        raise ValueError("version must use MAJOR.MINOR.PATCH")
    if channel not in CHANNELS:
        raise ValueError("channel is invalid")
    if not os.environ.get("POISON_RELEASE_PRIVATE_KEY_B64"):
        raise ValueError("POISON_RELEASE_PRIVATE_KEY_B64 is not configured")
    if not os.environ.get("POISON_RELEASE_KEY_ID"):
        raise ValueError("POISON_RELEASE_KEY_ID is not configured")
    if BUILD_LOCK.locked():
        raise RuntimeError("an artifact build is already running")
    threading.Thread(target=_build, args=(version, channel), daemon=True).start()


class Handler(BaseHTTPRequestHandler):
    def _json(self, status: int, payload: object) -> None:
        body = json.dumps(payload, sort_keys=True).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path == "/healthz":
            with STATE_LOCK:
                self._json(200 if STATE["status"] in {"ready", "idle"} else 503, dict(STATE))
            return
        relative = unquote(self.path.split("?", 1)[0]).lstrip("/") or "index.html"
        target = (OUTPUT / relative).resolve()
        if OUTPUT not in target.parents and target != OUTPUT:
            self._json(400, {"error": "unsafe artifact path"})
            return
        if not target.is_file():
            self._json(404, {"error": "artifact not found"})
            return
        data = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "application/gzip" if target.suffix == ".tgz" else "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self) -> None:
        if self.path != "/v1/build":
            self._json(404, {"error": "endpoint not found"})
            return
        if not TOKEN or self.headers.get("Authorization") != f"Bearer {TOKEN}":
            self._json(401, {"error": "invalid build token"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length))
            _start_build(str(payload["version"]), str(payload.get("channel", "developer")))
        except (ValueError, KeyError, json.JSONDecodeError, RuntimeError) as error:
            self._json(400, {"error": str(error)})
            return
        self._json(202, {"status": "building", "version": payload["version"], "channel": payload.get("channel", "developer")})

    def log_message(self, format: str, *args: object) -> None:
        return


def main() -> None:
    initial_version = os.environ.get("POISON_RELEASE_VERSION")
    if initial_version:
        _start_build(initial_version, os.environ.get("POISON_RELEASE_CHANNEL", "developer"))
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    server.serve_forever()


if __name__ == "__main__":
    main()
