#!/usr/bin/env python3
"""Black-box demo client for the HTTP and TCP protocols.

Only Python's standard library is used so the Compose demo has no package-install
step.  Every run writes JSON, JUnit and Markdown evidence, including failures.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import time
import traceback
import urllib.error
import urllib.parse
import urllib.request
import uuid
import xml.etree.ElementTree as ET
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Callable


LOGIN, LOGIN_RSP = 1005, 1006
ADD_FRIEND, ADD_FRIEND_RSP, ADD_FRIEND_NOTIFY = 1009, 1010, 1011
AUTH_FRIEND, AUTH_FRIEND_RSP, AUTH_FRIEND_NOTIFY = 1013, 1014, 1015
TEXT, TEXT_RSP, TEXT_NOTIFY, TEXT_ACK = 1017, 1018, 1019, 1020
UPLOAD, UPLOAD_RSP, CHUNK, CHUNK_RSP = 1025, 1026, 1027, 1028
FINISH, FINISH_RSP, FILE_NOTIFY = 1029, 1030, 1031
DOWNLOAD, DOWNLOAD_CHUNK, DOWNLOAD_DONE = 1032, 1033, 1034


@dataclass
class Step:
    name: str
    status: str
    duration_ms: int
    detail: str = ""
    event: str = ""


class Report:
    def __init__(self) -> None:
        self.started = time.time()
        self.steps: list[Step] = []

    def run(self, name: str, event: str, fn: Callable[[], Any]) -> Any:
        started = time.monotonic()
        try:
            result = fn()
            self.steps.append(Step(name, "passed", int((time.monotonic() - started) * 1000), event=event))
            return result
        except Exception as exc:
            self.steps.append(Step(name, "failed", int((time.monotonic() - started) * 1000), str(exc), event))
            raise

    def write(self, directory: Path, failure: str = "") -> None:
        if failure and not any(step.status == "failed" for step in self.steps):
            self.steps.append(Step("unhandled scenario failure", "failed", 0, failure[-1000:]))
        directory.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime(self.started)) + "-" + uuid.uuid4().hex[:6]
        commit = os.getenv("E2E_GIT_COMMIT", "unknown")
        payload = {
            "schema_version": 1,
            "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(self.started)),
            "commit": commit,
            "environment": {"python": sys.version.split()[0], "platform": sys.platform},
            "passed": not failure and all(s.status == "passed" for s in self.steps),
            "failure": failure,
            "steps": [asdict(s) for s in self.steps],
        }
        (directory / f"e2e-{stamp}.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")

        suite = ET.Element("testsuite", name="distributed-chat-e2e", tests=str(len(self.steps)),
                           failures=str(sum(s.status != "passed" for s in self.steps)))
        for step in self.steps:
            case = ET.SubElement(suite, "testcase", name=step.name, time=f"{step.duration_ms / 1000:.3f}")
            if step.status != "passed":
                ET.SubElement(case, "failure", message=step.detail).text = step.detail
        ET.ElementTree(suite).write(directory / f"e2e-{stamp}.xml", encoding="utf-8", xml_declaration=True)

        rows = ["# 双节点聊天系统实测报告", "", f"- Commit: `{commit}`",
                f"- UTC: `{payload['started_utc']}`", f"- Result: **{'PASS' if payload['passed'] else 'FAIL'}**",
                "", "| 场景 | 结果 | 耗时 (ms) | 对应事件 |", "|---|---:|---:|---|"]
        rows.extend(f"| {s.name} | {s.status} | {s.duration_ms} | `{s.event}` |" for s in self.steps)
        if failure:
            rows.extend(["", "## Failure", "", "```text", failure[-4000:], "```"])
        (directory / f"e2e-{stamp}.md").write_text("\n".join(rows) + "\n", encoding="utf-8")


def http_json(base: str, path: str, body: dict[str, Any], timeout: float = 20) -> tuple[dict[str, Any], str]:
    request = urllib.request.Request(base + path, data=json.dumps(body).encode(),
                                     headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(request, timeout=timeout) as response:
        request_id = response.headers.get("X-Request-Id", "")
        parsed = json.loads(response.read().decode())
    if not request_id:
        raise AssertionError(f"{path} did not return X-Request-Id")
    return parsed, request_id


def wait_http(url: str, seconds: int = 120) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=2):
                return
        except Exception:
            time.sleep(1)
    raise TimeoutError(f"service not ready: {url}")


def verification_code(mailpit: str, email: str, seconds: int = 30) -> str:
    deadline = time.monotonic() + seconds
    query = urllib.parse.quote(f"to:{email}")
    while time.monotonic() < deadline:
        with urllib.request.urlopen(f"{mailpit}/api/v1/search?query={query}", timeout=5) as response:
            listing = json.loads(response.read().decode())
        messages = listing.get("messages", listing.get("Messages", []))
        if messages:
            message_id = messages[0].get("ID", messages[0].get("id"))
            with urllib.request.urlopen(f"{mailpit}/api/v1/message/{message_id}", timeout=5) as response:
                message = json.loads(response.read().decode())
            text = json.dumps(message, ensure_ascii=False)
            import re
            match = re.search(r"(?<!\d)(\d{6})(?!\d)", text)
            if match:
                return match.group(1)
        time.sleep(1)
    raise TimeoutError(f"verification email for {email} was not received")


class ChatClient:
    def __init__(self, host: str, port: int) -> None:
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.settimeout(10)
        self.buffer = bytearray()
        self.pending = []

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass

    def send(self, message_id: int, body: dict[str, Any]) -> None:
        payload = json.dumps(body, separators=(",", ":")).encode()
        if len(payload) > 65535:
            raise ValueError("frame too large")
        self.sock.sendall(struct.pack("!HH", message_id, len(payload)) + payload)

    def receive(self, wanted: set[int] | None = None, timeout: float = 10) -> tuple[int, dict[str, Any]]:
        deadline = time.monotonic() + timeout
        for index, (message_id, body) in enumerate(self.pending):
            if wanted is None or message_id in wanted:
                return self.pending.pop(index)
        while True:
            if len(self.buffer) >= 4:
                message_id, length = struct.unpack("!HH", self.buffer[:4])
                if len(self.buffer) >= 4 + length:
                    body = json.loads(self.buffer[4:4+length].decode())
                    del self.buffer[:4+length]
                    if wanted is None or message_id in wanted:
                        return message_id, body
                    self.pending.append((message_id, body))
                    if len(self.pending) > 4096:
                        raise AssertionError("unhandled frame backlog exceeded 4096")
                    continue
            remaining = deadline - time.monotonic()
            if remaining <= 0: raise TimeoutError(f"did not receive one of {wanted}")
            self.sock.settimeout(remaining)
            chunk = self.sock.recv(65536)
            if not chunk: raise ConnectionError("chat server closed the connection")
            self.buffer.extend(chunk)

    def barrier(self) -> None:
        # Business frames share a FIFO shard. This proves earlier ACK callbacks
        # completed before closing the socket; it does not invent an ACK response.
        nonce = uuid.uuid4().hex
        self.send(1023, {"nonce": nonce})
        if self.receive({1024})[1].get("nonce") != nonce:
            raise AssertionError("heartbeat barrier mismatch")


def assert_ok(value: dict[str, Any], context: str) -> None:
    if value.get("error") != 0:
        raise AssertionError(f"{context}: {value}")


def register(gate: str, mailpit: str, email: str, name: str, password: str) -> int:
    sent, _ = http_json(gate, "/post_email", {"email": email})
    assert_ok(sent, "request verification code")
    code = verification_code(mailpit, email)
    created, _ = http_json(gate, "/user_register", {
        "email": email, "user": name, "passwd": password, "icon": "", "varifycode": code,
    })
    assert_ok(created, "register")
    return int(created["uid"])


def connect_user(gate: str, email: str, password: str) -> tuple[ChatClient, dict[str, Any]]:
    login, _ = http_json(gate, "/user_login", {"email": email, "passwd": password})
    assert_ok(login, "HTTP login")
    client = ChatClient(login["host"], int(login["port"]))
    client.send(LOGIN, {"uid": int(login["uid"]), "token": login["token"]})
    _, response = client.receive({LOGIN_RSP})
    assert_ok(response, "TCP ticket authentication")
    return client, login


def no_message(client: ChatClient, wanted: set[int], seconds: float) -> None:
    try:
        client.receive(wanted, timeout=seconds)
    except (TimeoutError, socket.timeout):
        return
    raise AssertionError(f"unexpected message {wanted}")


def scenario(args: argparse.Namespace, report: Report) -> None:
    wait_http(args.gate + "/get_test")
    suffix = f"{int(time.time())}-{uuid.uuid4().hex[:6]}"
    password = "Demo-pass-2026!"
    first = {"email": f"alice-{suffix}@example.test", "name": f"alice_{suffix[-6:]}", "password": password}
    second = {"email": f"bob-{suffix}@example.test", "name": f"bob_{suffix[-6:]}", "password": password}
    first["uid"] = report.run("注册用户 A", "varify.code_sent", lambda: register(args.gate, args.mailpit, first["email"], first["name"], password))
    second["uid"] = report.run("注册用户 B", "varify.code_sent", lambda: register(args.gate, args.mailpit, second["email"], second["name"], password))

    alice, alice_login = report.run("登录并认证 A", "auth.session_authenticated", lambda: connect_user(args.gate, first["email"], password))
    bob, bob_login = report.run("登录并认证 B", "auth.session_authenticated", lambda: connect_user(args.gate, second["email"], password))
    try:
        allow_single = os.getenv("E2E_ALLOW_SINGLE_NODE", "0") == "1"
        report.run("双节点路由" if not allow_single else "存活节点路由", "routing.server_selected", lambda: (
            None if allow_single or alice_login["host"] != bob_login["host"] else
            (_ for _ in ()).throw(AssertionError(f"both users routed to {alice_login['host']}"))))

        def become_friends() -> None:
            alice.send(ADD_FRIEND, {"touid": second["uid"], "applyname": first["name"], "bakname": "Bob"})
            assert_ok(alice.receive({ADD_FRIEND_RSP})[1], "friend application")
            bob.receive({ADD_FRIEND_NOTIFY})
            bob.send(AUTH_FRIEND, {"touid": first["uid"], "back": "Alice"})
            assert_ok(bob.receive({AUTH_FRIEND_RSP})[1], "friend acceptance")
            alice.receive({AUTH_FRIEND_NOTIFY})
        report.run("跨节点建立好友关系", "friend.accepted", become_friends)

        msgid = uuid.uuid4().hex
        def online_message() -> None:
            alice.send(TEXT, {"touid": second["uid"], "text_array": [{"msgid": msgid, "content": "observable hello"}]})
            assert_ok(alice.receive({TEXT_RSP})[1], "message persistence")
            delivered = bob.receive({TEXT_NOTIFY})[1]
            if delivered["text_array"][0]["msgid"] != msgid:
                raise AssertionError("message id changed in transit")
            bob.send(TEXT_ACK, {"fromuid": first["uid"], "msgids": [msgid]})
            bob.barrier()
        report.run("跨节点消息与 ACK", "message.acknowledged", online_message)

        bob.close()
        offline_id = uuid.uuid4().hex
        def offline_submit() -> None:
            body = {"touid": second["uid"], "text_array": [{"msgid": offline_id, "content": "offline once"}]}
            alice.send(TEXT, body); assert_ok(alice.receive({TEXT_RSP})[1], "offline persistence")
            alice.send(TEXT, body); assert_ok(alice.receive({TEXT_RSP})[1], "idempotent duplicate")
        report.run("离线持久化与重复提交幂等", "message.persisted", offline_submit)

        bob, _ = connect_user(args.gate, second["email"], password)
        delivered = report.run("重连至少一次重投", "message.pending_delivered", lambda: bob.receive({TEXT_NOTIFY})[1])
        if delivered["text_array"][0]["msgid"] != offline_id:
            raise AssertionError("wrong pending message")
        bob.send(TEXT_ACK, {"fromuid": first["uid"], "msgids": [offline_id]})
        bob.barrier()
        bob.close(); bob, _ = connect_user(args.gate, second["email"], password)
        report.run("ACK 后不再重投", "message.acknowledged", lambda: no_message(bob, {TEXT_NOTIFY}, 2.0))

        def lost_ack() -> None:
            retry_id = uuid.uuid4().hex
            alice.send(TEXT, {"touid": second["uid"], "text_array": [{"msgid": retry_id, "content": "retry while online"}]})
            assert_ok(alice.receive({TEXT_RSP})[1], "retry message acceptance")
            first_delivery = bob.receive({TEXT_NOTIFY})[1]
            repeated = bob.receive({TEXT_NOTIFY}, timeout=15)[1]
            if first_delivery["text_array"][0]["msgid"] != retry_id or repeated != first_delivery:
                raise AssertionError("lost ACK did not replay identical durable message")
            bob.send(TEXT_ACK, {"fromuid": first["uid"], "msgids": [retry_id]})
            bob.barrier()
            no_message(bob, {TEXT_NOTIFY}, 6)
        report.run("online retry after lost ACK", "message.retry", lost_ack)

        bob.close()
        expected_ids = [uuid.uuid4().hex for _ in range(args.backlog)]
        def create_backlog() -> None:
            for offset in range(0, len(expected_ids), 10):
                messages = [{"msgid": value, "content": "backlog"} for value in expected_ids[offset:offset+10]]
                alice.send(TEXT, {"touid": second["uid"], "text_array": messages})
                assert_ok(alice.receive({TEXT_RSP})[1], "backlog acceptance")
        report.run("persist large offline backlog", "message.persisted", create_backlog)
        bob, _ = connect_user(args.gate, second["email"], password)
        def drain_backlog() -> None:
            seen = set()
            expected = set(expected_ids)
            deadline = time.monotonic() + 120
            while seen != expected:
                if time.monotonic() >= deadline: raise TimeoutError("backlog did not drain")
                value = bob.receive({TEXT_NOTIFY}, timeout=min(15, deadline-time.monotonic()))[1]
                ids = [message["msgid"] for message in value["text_array"]]
                if any(item not in expected for item in ids): raise AssertionError("unexpected pending message")
                seen.update(ids)
                bob.send(TEXT_ACK, {"fromuid": first["uid"], "msgids": ids})
            bob.barrier()
        report.run("ACK-driven refill without reconnect", "message.backlog_drained", drain_backlog)
        bob.close(); bob, _ = connect_user(args.gate, second["email"], password)
        report.run("backlog remains acknowledged after reconnect", "message.acknowledged", lambda: no_message(bob, {TEXT_NOTIFY}, 2))

        content = (b"distributed-chat-encrypted-payload-" * 256) + os.urandom(47)
        digest = hashlib.sha256(content).hexdigest()
        transfer: dict[str, Any] = {}
        def interrupted_upload() -> None:
            alice.send(UPLOAD, {"touid": second["uid"], "name": "evidence.bin", "mime": "application/octet-stream",
                                "total_size": len(content), "sha256": digest})
            response = alice.receive({UPLOAD_RSP})[1]; assert_ok(response, "begin upload")
            transfer["id"] = response["id"]
            half = len(content) // 2
            alice.send(CHUNK, {"id": transfer["id"], "offset": 0,
                               "data": base64.b64encode(content[:half]).decode()})
            assert_ok(alice.receive({CHUNK_RSP})[1], "first upload chunk")
            alice.close()
        report.run("文件上传中断", "file.upload_progress", interrupted_upload)

        alice, _ = connect_user(args.gate, first["email"], password)
        def resume_and_download() -> None:
            metadata = {"touid": second["uid"], "name": "evidence.bin", "mime": "application/octet-stream",
                        "total_size": len(content), "sha256": digest, "id": transfer["id"]}
            alice.send(UPLOAD, metadata)
            resumed = alice.receive({UPLOAD_RSP})[1]; assert_ok(resumed, "resume upload")
            offset = int(resumed["offset"])
            if offset <= 0:
                raise AssertionError("server did not retain upload offset")
            alice.send(CHUNK, {"id": transfer["id"], "offset": offset,
                               "data": base64.b64encode(content[offset:]).decode()})
            assert_ok(alice.receive({CHUNK_RSP})[1], "final upload chunk")
            alice.send(FINISH, {"id": transfer["id"]})
            assert_ok(alice.receive({FINISH_RSP})[1], "finish upload")
            bob.receive({FILE_NOTIFY})
            downloaded = bytearray(); offset = 0
            while offset < len(content):
                bob.send(DOWNLOAD, {"id": transfer["id"], "offset": offset})
                chunk = bob.receive({DOWNLOAD_CHUNK})[1]; assert_ok(chunk, "download chunk")
                downloaded.extend(base64.b64decode(chunk["data"])); offset = int(chunk["next_offset"])
            bob.send(DOWNLOAD_DONE, {"id": transfer["id"]})
            if hashlib.sha256(downloaded).hexdigest() != digest:
                raise AssertionError("download checksum mismatch")
            storage = Path(args.storage)
            if storage.exists():
                marker = content[:256]
                for path in storage.rglob("*"):
                    if path.is_file() and marker in path.read_bytes():
                        raise AssertionError(f"plaintext marker found in {path}")
        report.run("按 offset 续传、下载摘要及落盘加密", "file.download_complete", resume_and_download)
    finally:
        alice.close(); bob.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gate", default="http://127.0.0.1:8080")
    parser.add_argument("--mailpit", default="http://127.0.0.1:8025")
    parser.add_argument("--storage", default="/files")
    parser.add_argument("--backlog", type=int, default=int(os.getenv("E2E_BACKLOG", "1000")))
    args = parser.parse_args()
    if args.backlog < 201: parser.error("backlog must exceed the old 200-message limit")
    report = Report(); failure = ""
    try:
        scenario(args, report)
    except Exception:
        failure = traceback.format_exc()
        print(failure, file=sys.stderr)
    finally:
        report.write(Path(os.getenv("E2E_REPORT_DIR", "build/demo/reports")), failure)
    return 1 if failure else 0


if __name__ == "__main__":
    raise SystemExit(main())
