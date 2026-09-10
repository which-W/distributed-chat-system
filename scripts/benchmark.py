#!/usr/bin/env python3
"""Fresh login tickets per run; secrets never enter benchmark reports."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import hashlib
import platform
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/e2e"))
from chat_e2e import (register, connect_user, http_json, assert_ok,
                      ADD_FRIEND, ADD_FRIEND_RSP, ADD_FRIEND_NOTIFY,
                      AUTH_FRIEND, AUTH_FRIEND_RSP, AUTH_FRIEND_NOTIFY)


def prepare(args):
    if args.accounts.exists():
        raise ValueError("refusing to overwrite existing benchmark accounts")
    accounts = []
    for pair in range(args.connections // 2):
        suffix = uuid.uuid4().hex[:12]
        users = []
        for side in ("a", "b"):
            user = {"email": f"bench-{side}-{suffix}@example.test",
                    "password": "Bench!" + uuid.uuid4().hex}
            user["uid"] = register(args.gate, args.mailpit, user["email"], side + suffix, user["password"])
            users.append(user)
        a, _ = connect_user(args.gate, users[0]["email"], users[0]["password"])
        b, _ = connect_user(args.gate, users[1]["email"], users[1]["password"])
        try:
            a.send(ADD_FRIEND, {"touid": users[1]["uid"], "applyname": "bench", "bakname": "bench"})
            assert_ok(a.receive({ADD_FRIEND_RSP})[1], "friend request")
            b.receive({ADD_FRIEND_NOTIFY})
            b.send(AUTH_FRIEND, {"touid": users[0]["uid"], "back": "bench"})
            assert_ok(b.receive({AUTH_FRIEND_RSP})[1], "friend acceptance")
            a.receive({AUTH_FRIEND_NOTIFY})
        finally:
            a.close(); b.close()
        users[0]["peer_uid"] = users[1]["uid"]
        users[1]["peer_uid"] = users[0]["uid"]
        accounts.extend(users)
    args.accounts.parent.mkdir(parents=True, exist_ok=True)
    with args.accounts.open("x", encoding="utf-8") as output:
        os.chmod(args.accounts, 0o600)
        json.dump(accounts, output)
    return accounts


def fresh_sessions(gate, accounts):
    started = time.monotonic()
    def login(user):
        value, _ = http_json(gate, "/user_login", {"email": user["email"], "passwd": user["password"]})
        assert_ok(value, "benchmark login")
        if value.get("transport", "tcp") not in ("tcp", "plain", "insecure", ""):
            raise ValueError("C++ loadgen requires the private demo TCP listener; TLS is not measured")
        if int(value["uid"]) != int(user["uid"]):
            raise ValueError("benchmark account identity changed")
        return {key: value[key] for key in ("host", "port", "uid", "token")} | {"peer_uid": user["peer_uid"]}
    with ThreadPoolExecutor(max_workers=4) as pool:
        sessions = list(pool.map(login, accounts))
    if time.monotonic() - started > 45:
        raise TimeoutError("ticket preparation exceeded 45 seconds; reduce connections")
    return sessions


def sample_resources(stop, path):
    with path.open("w", encoding="utf-8") as output:
        while not stop.is_set():
            try:
                result = subprocess.run(["docker", "stats", "--no-stream", "--format", "{{json .}}"],
                                        capture_output=True, text=True, timeout=5)
                if result.returncode:
                    raise RuntimeError("docker stats failed")
                for line in result.stdout.splitlines():
                    output.write(json.dumps({"time": time.time(), "stats": json.loads(line)}) + "\n")
            except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
                output.write(json.dumps({"time": time.time(), "unavailable": type(exc).__name__}) + "\n")
            output.flush()
            stop.wait(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--accounts", required=True, type=Path)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--gate", default="http://127.0.0.1:8080")
    parser.add_argument("--mailpit", default="http://127.0.0.1:8025")
    parser.add_argument("--loadgen", default=os.getenv("CHAT_LOADGEN", str(ROOT / "build/linux-server-release/bin/chat_loadgen")))
    parser.add_argument("--label", default="candidate")
    parser.add_argument("--output-root", type=Path, default=ROOT / "build/bench")
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--connections", type=int)
    parser.add_argument("--rate", type=int)
    parser.add_argument("--rounds", type=int)
    args = parser.parse_args()
    args.connections = args.connections or (2 if args.smoke else 20)
    args.rate = args.rate or (20 if args.smoke else 500)
    args.rounds = args.rounds or (1 if args.smoke else 3)
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label):
        raise ValueError("label must contain only letters, digits, underscore or hyphen")
    if args.connections < 2 or args.connections % 2 or args.rate < 1 or args.rounds < 1:
        raise ValueError("use an even connection count and positive rate/rounds")
    accounts = prepare(args) if args.prepare else json.loads(args.accounts.read_text(encoding="utf-8"))
    if len(accounts) != args.connections or len({u["uid"] for u in accounts}) != args.connections:
        raise ValueError("one unique account per connection is required")
    output = args.output_root / args.label
    output.mkdir(parents=True, exist_ok=False)  # Never mix old rounds into a new result.
    metadata = {"schema_version": 2, "connections": args.connections, "rate": args.rate,
                "rounds": args.rounds, "smoke": args.smoke, "python": sys.version,
                "measurement": "closed-loop server acceptance; excludes login; no TLS",
                "resources": "see docker-stats JSONL; unavailable samples are explicit"}
    metadata["platform"] = platform.platform()
    metadata["loadgen_sha256"] = hashlib.sha256(Path(args.loadgen).read_bytes()).hexdigest()
    metadata["compose_sha256"] = hashlib.sha256((ROOT / "compose.demo.yaml").read_bytes()).hexdigest()
    try:
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True, timeout=5, check=True)
        status = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True, timeout=5, check=True)
        metadata["source_revision"] = revision.stdout.strip()
        metadata["source_dirty"] = bool(status.stdout.strip())
    except (OSError, subprocess.SubprocessError):
        metadata["source_revision"] = "unavailable"
    (output / "environment.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    runs = [("warmup", 3 if args.smoke else 30)] + [
        (f"round-{i}", 5 if args.smoke else 60) for i in range(1, args.rounds + 1)]
    for index, (name, duration) in enumerate(runs):
        sessions = fresh_sessions(args.gate, accounts)
        # Each round gets a freshly issued set, including after a long warmup.
        fd, ticket_path = tempfile.mkstemp(prefix="chat-tickets-", suffix=".json")
        stop = threading.Event()
        sampler = threading.Thread(target=sample_resources, args=(stop, output / f"docker-stats-{name}.jsonl"))
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as ticket_file:
                json.dump(sessions, ticket_file)
            sampler.start()
            command = [args.loadgen, "--sessions", ticket_path, "--connections", str(args.connections),
                       "--rate", str(args.rate), "--batch", "1", "--duration", str(duration),
                       "--timeout-ms", "5000", "--seed", str(20260905 + index)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=duration + 30)
            (output / f"{name}.json").write_text(result.stdout, encoding="utf-8")
            if result.returncode:
                raise RuntimeError(f"loadgen failed in {name}; inspect its result JSON")
            value = json.loads(result.stdout)
            if value.get("schema_version") != 2:
                raise ValueError("use the current loadgen against both baseline and candidate servers")
        finally:
            stop.set()
            if sampler.ident is not None: sampler.join()
            Path(ticket_path).unlink(missing_ok=True)
    print(output)


if __name__ == "__main__":
    main()
