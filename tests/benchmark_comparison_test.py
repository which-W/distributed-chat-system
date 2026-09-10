import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/compare-benchmarks.py"


class ComparisonTest(unittest.TestCase):
    def test_reject_incomplete_or_incomparable_results(self):
        valid = dict(schema_version=2, mode="closed_loop_acceptance", setup_errors=0,
                     accepted_messages=10, attempted_messages=10, failed_messages=0,
                     connections=2, configured_rate=20, batch=1, seed=42,
                     throughput=20, accept_p50_ms=1, accept_p95_ms=2, accept_p99_ms=3, error_rate=0)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            before, after = root / "before", root / "after"
            before.mkdir(); after.mkdir()
            (before / "round-1.json").write_text(json.dumps(valid))
            for changes, success in [({}, True), ({"failed_messages": 1}, False),
                                     ({"error_rate": .1}, False), ({"attempted_messages": 11}, False),
                                     ({"seed": 43}, False), ({"accept_p99_ms": float("nan")}, False)]:
                with self.subTest(changes=changes):
                    (after / "round-1.json").write_text(json.dumps(valid | changes))
                    result = subprocess.run([sys.executable, str(SCRIPT), str(before), str(after),
                                             "--output", str(root / "report.md")], capture_output=True, timeout=10)
                    self.assertEqual(result.returncode == 0, success, result.stderr.decode())


if __name__ == "__main__":
    unittest.main()
