#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/e2e"))
from chat_e2e import Report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("env_file", nargs="?", type=Path, default=ROOT / "build/demo/demo.env")
    args = parser.parse_args()
    if not args.env_file.exists(): parser.error("run scripts/demo.sh --keep first")
    name = "fault-" + uuid.uuid4().hex[:12]
    crash_container = name + "-crash"
    directory = ROOT / "build/demo/reports" / name
    directory.mkdir(parents=True)
    compose = ["docker", "compose", "--project-directory", str(ROOT), "--env-file", str(args.env_file),
               "-f", str(ROOT / "compose.demo.yaml")]
    probe_base = ["--profile", "tools", "run", "--rm", "e2e", "python", "tests/e2e/fault_probe.py"]
    def probe(mode, *extra):
        base = probe_base
        if mode == "crash":
            base = ["--profile", "tools", "run", "--rm", "--name", crash_container,
                    "e2e", "python", "tests/e2e/fault_probe.py"]
        return compose + base + [mode, "--state", f"/state/{name}.json",
                                       "--reports", f"/reports/{name}"] + list(extra)
    def command(argv, timeout=150):
        subprocess.run(argv, check=True, timeout=timeout)
    report = Report(); failure = ""; victim = None; stopped = set(); process = None
    try:
        report.run("create and verify real accounts", "fault.prepare", lambda: command(probe("prepare")))
        def chat_crash():
            nonlocal process, victim
            process = subprocess.Popen(probe("crash"))
            deadline = time.monotonic() + 60
            marker = directory / "ready.json"
            while not marker.exists():
                if process.poll() is not None: raise RuntimeError("crash probe exited before ready")
                if time.monotonic() >= deadline: raise TimeoutError("crash probe never became ready")
                time.sleep(.1)
            # Atomicity of a short file write is not assumed.
            while True:
                try: value = json.loads(marker.read_text()); break
                except ValueError:
                    if time.monotonic() >= deadline: raise
                    time.sleep(.05)
            victim = value["victim"]
            if victim not in ("chatserver1", "chatserver2") or value["accepted"] < 20:
                raise AssertionError("invalid crash readiness evidence")
            stopped.add(victim)
            command(compose + ["kill", "-s", "SIGKILL", victim])
            # Poll a real login until the stale health key expires and failover works.
            command(probe("recover"))
            (directory / "resume").touch()
            if process.wait(timeout=120): raise RuntimeError("message recovery probe failed")
            command(compose + ["start", victim]); stopped.remove(victim)
            command(probe("recover"))
        report.run("SIGKILL during traffic and reconcile accepted IDs", "fault.chat_crash", chat_crash)
        for dependency in ("redis", "mysql"):
            def outage(dependency=dependency):
                command(probe("recover"))  # Demonstrate success immediately before fault.
                stopped.add(dependency)
                command(compose + ["stop", dependency])
                command(probe("dependency", "--dependency", dependency), timeout=40)
                command(compose + ["start", dependency]); stopped.remove(dependency)
                command(probe("recover"))
            report.run(dependency + " explicit failure and recovery", "fault." + dependency, outage)
        report.run("full E2E after all recovery", "fault.e2e",
            lambda: command(compose + ["--profile", "tools", "run", "--rm", "e2e"], timeout=300))
    except Exception as exc:
        failure = f"{type(exc).__name__}: {exc}"
        print(failure, file=sys.stderr)
    finally:
        # Restore only services this invocation stopped; never destroy user volumes.
        for service in stopped:
            try: command(compose + ["start", service], timeout=45)
            except Exception: failure = failure or "failed to restore service " + service
        if process and process.poll() is None:
            (directory / "resume").touch()
            try: process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                failure = failure or "crash probe required termination"
                try:
                    command(["docker", "rm", "-f", crash_container], timeout=15)
                except Exception:
                    failure += "; failed to remove crash probe container " + crash_container
                process.terminate()
                try: process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(timeout=10)
        try: command(probe("cleanup"), timeout=30)
        except Exception: failure = failure or "private probe state cleanup failed"
        report.write(directory, failure)
    print(directory)
    return 1 if failure else 0


if __name__ == "__main__":
    raise SystemExit(main())
