#!/usr/bin/env python3
"""Runs inside the private Compose network; credentials live only in /state."""
import argparse
import json
import os
from pathlib import Path
import sys
import time
import traceback
import uuid

from chat_e2e import (Report, connect_user, http_json, assert_ok, no_message,
                      TEXT, TEXT_RSP, TEXT_NOTIFY, TEXT_ACK)
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from benchmark import prepare


def load(args):
    return json.loads(args.state.read_text(encoding="utf-8"))


def recover(args):
    user = load(args)[0]
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        try:
            client, _ = connect_user(args.gate, user["email"], user["password"])
            client.barrier(); client.close()
            return
        except (OSError, AssertionError, ConnectionError):
            time.sleep(1)
    raise TimeoutError("existing account did not recover within 90 seconds")


def dependency(args):
    user = load(args)[0]
    started = time.monotonic()
    value, _ = http_json(args.gate, "/user_login",
                         {"email": user["email"], "passwd": user["password"]}, timeout=12)
    if time.monotonic() - started > 12:
        raise AssertionError("dependency failure exceeded response deadline")
    if value.get("error") != 1002:
        raise AssertionError("expected dependency error 1002 for a previously valid account")
    if args.dependency == "mysql" and value.get("retryable") is not True:
        raise AssertionError("MySQL outage was incorrectly reported as invalid credentials")


def crash(args):
    users = load(args)
    sender, login = connect_user(args.gate, users[0]["email"], users[0]["password"])
    receiver = None
    prefix = uuid.uuid4().hex
    attempted, accepted_before = [], []
    def submit(client, msgid):
        client.send(TEXT, {"touid": users[1]["uid"],
                           "text_array": [{"msgid": msgid, "content": "crash recovery"}]})
        assert_ok(client.receive({TEXT_RSP}, timeout=8)[1], "durable acceptance")
    try:
        for i in range(20):
            msgid = f"{prefix}-{i}"; attempted.append(msgid)
            submit(sender, msgid); accepted_before.append(msgid)
        victim = login["host"]
        if victim not in ("chatserver1", "chatserver2"):
            raise AssertionError("unexpected victim hostname")
        (args.reports / "ready.json").write_text(json.dumps({"victim": victim, "accepted": len(accepted_before)}))
        interrupted = False
        for i in range(20, 500):
            msgid = f"{prefix}-{i}"; attempted.append(msgid)
            try:
                submit(sender, msgid); accepted_before.append(msgid)
            except (OSError, AssertionError, ConnectionError):
                interrupted = True
                break
            time.sleep(.02)
        if not interrupted:
            raise AssertionError("controller did not interrupt active traffic")
        deadline = time.monotonic() + 120
        while not (args.reports / "resume").exists():
            if time.monotonic() >= deadline: raise TimeoutError("controller did not resume scenario")
            time.sleep(.1)
        sender.close()
        sender, _ = connect_user(args.gate, users[0]["email"], users[0]["password"])
        # Replay identical IDs, including the message with an ambiguous response.
        for msgid in attempted: submit(sender, msgid)
        receiver, _ = connect_user(args.gate, users[1]["email"], users[1]["password"])
        expected, seen = set(attempted), set()
        deadline = time.monotonic() + 90
        while seen != expected:
            if time.monotonic() >= deadline: raise TimeoutError("accepted messages were not recovered")
            value = receiver.receive({TEXT_NOTIFY}, timeout=15)[1]
            ids = [item["msgid"] for item in value["text_array"]]
            if any(item not in expected for item in ids): raise AssertionError("unexpected replay identity")
            seen.update(ids)
            receiver.send(TEXT_ACK, {"fromuid": users[0]["uid"], "msgids": ids})
        receiver.barrier(); receiver.close()
        receiver, _ = connect_user(args.gate, users[1]["email"], users[1]["password"])
        no_message(receiver, {TEXT_NOTIFY}, 3)
        (args.reports / "message-accounting.json").write_text(json.dumps({
            "accepted_before_crash": len(accepted_before), "attempted_ids": len(expected),
            "recovered_unique_ids": len(seen), "interruption_observed": interrupted}))
    finally:
        sender.close()
        if receiver: receiver.close()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("mode", choices=("prepare", "recover", "dependency", "crash", "cleanup"))
    p.add_argument("--state", type=Path, required=True)
    p.add_argument("--reports", type=Path, required=True)
    p.add_argument("--dependency", choices=("mysql", "redis"))
    p.add_argument("--gate", default="http://gate:8080")
    p.add_argument("--mailpit", default="http://mailpit:8025")
    args = p.parse_args()
    args.reports.mkdir(parents=True, exist_ok=True)
    report = Report(); failure = ""
    def run():
        if args.mode == "prepare":
            args.accounts = args.state; args.connections = 2
            prepare(args); recover(args)
        elif args.mode == "cleanup": args.state.unlink(missing_ok=True)
        elif args.mode == "dependency": dependency(args)
        elif args.mode == "recover": recover(args)
        else: crash(args)
    try:
        report.run(args.mode + " " + (args.dependency or ""), "fault." + args.mode, run)
    except Exception:
        failure = traceback.format_exc()
        print(failure, file=sys.stderr)
    finally:
        report.write(args.reports, failure)
    return 1 if failure else 0


if __name__ == "__main__":
    raise SystemExit(main())
