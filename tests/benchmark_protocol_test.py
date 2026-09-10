#!/usr/bin/env python3
"""Loopback tests exercise the compiled load generator, without databases."""
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

LOADGEN = sys.argv.pop(1)
sys.path.insert(0, str(Path(__file__).parent / "e2e"))
from chat_e2e import ChatClient


def read_frame(connection):
    def exact(n):
        data = b""
        while len(data) < n:
            part = connection.recv(n - len(data))
            if not part: raise EOFError()
            data += part
        return data
    kind, length = struct.unpack("!HH", exact(4))
    return kind, json.loads(exact(length))


def frame(kind, body):
    data = json.dumps(body).encode()
    return struct.pack("!HH", kind, len(data)) + data


class ProtocolTests(unittest.TestCase):
    def run_load(self, behavior):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0)); listener.listen(); listener.settimeout(5)
        port = listener.getsockname()[1]
        def server():
            try:
                connection, _ = listener.accept()
                with connection:
                    connection.settimeout(3)
                    read_frame(connection)
                    if behavior == "login-stall":
                        time.sleep(.8); return
                    connection.sendall(frame(1006, {"error": 0}))
                    while True:
                        kind, request = read_frame(connection)
                        if kind == 1020: continue
                        if behavior == "read-stall":
                            time.sleep(.8); return
                        reply = {"error": 0, "delivery": "accepted", "text_array": request["text_array"]}
                        if behavior == "reject": reply["error"] = 1002
                        if behavior == "wrong-id": reply["text_array"] = [{"msgid": "other", "content": "x"}]
                        connection.sendall(frame(1018, reply))
            except (OSError, EOFError):
                pass
        thread = threading.Thread(target=server)
        thread.start()
        try:
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "sessions.json"
                path.write_text(json.dumps([{"host": "127.0.0.1", "port": port, "uid": 1,
                                              "peer_uid": 2, "token": "synthetic-ticket"}]))
                started = time.monotonic()
                result = subprocess.run([LOADGEN, "--sessions", str(path), "--connections", "1",
                                         "--duration", "1", "--rate", "20", "--batch", "2",
                                         "--timeout-ms", "200"], capture_output=True, text=True, timeout=5)
                elapsed = time.monotonic() - started
                return result.returncode, json.loads(result.stdout), elapsed
        finally:
            listener.close(); thread.join(timeout=4)
            self.assertFalse(thread.is_alive(), "stub server leaked a thread")

    def test_success_excludes_login_and_counts_messages(self):
        code, row, _ = self.run_load("accept")
        self.assertEqual(code, 0)
        self.assertGreater(row["accepted_messages"], 0)
        self.assertEqual(row["attempted_messages"], row["accepted_messages"])
        self.assertEqual(row["failed_messages"], 0)
        self.assertIn("accept_p99_ms", row)

    def test_rejection_is_not_successful_throughput(self):
        for behavior in ("reject", "wrong-id", "read-stall"):
            with self.subTest(behavior=behavior):
                code, row, elapsed = self.run_load(behavior)
                self.assertNotEqual(code, 0)
                self.assertEqual(row["accepted_messages"], 0)
                self.assertEqual(row["failed_messages"], 2)
                self.assertEqual(row["error_rate"], 1)
                self.assertLess(elapsed, 3)

    def test_login_timeout_is_setup_failure(self):
        code, row, elapsed = self.run_load("login-stall")
        self.assertNotEqual(code, 0)
        self.assertEqual(row["setup_errors"], 1)
        self.assertEqual(row["attempted_messages"], 0)
        self.assertLess(elapsed, 3)

    def test_partial_frame_survives_timeout_and_unmatched_frames_are_retained(self):
        listener = socket.socket(); listener.bind(("127.0.0.1", 0)); listener.listen()
        client = ChatClient("127.0.0.1", listener.getsockname()[1])
        server, _ = listener.accept()
        try:
            payload = frame(1019, {"payload": "pending"})
            server.sendall(payload[:2])
            with self.assertRaises(TimeoutError): client.receive({1019}, timeout=.02)
            server.sendall(payload[2:] + frame(1024, {"nonce": "ok"}))
            self.assertEqual(client.receive({1024})[1]["nonce"], "ok")
            self.assertEqual(client.receive({1019})[1]["payload"], "pending")
        finally:
            client.close(); server.close(); listener.close()


if __name__ == "__main__":
    unittest.main()
