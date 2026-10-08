"""CPP-A20: real child-process/FileJournal crash windows; synthetic transport only."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    temp = Path(tempfile.gettempdir()).resolve(strict=True)
    root = Path(tempfile.mkdtemp(prefix="tansr-cpp-executor-crash-")).resolve(strict=True)
    assert root.parent == temp
    report = {
        "binary": str(binary), "binarySha256": digest(binary),
        "kind": "native-child-process-real-private-journal-synthetic-transport",
        "workspace": str(root), "cases": [], "cleaned": False,
    }
    try:
        for phase in ["before-claim", "after-claim", "after-handler", "after-receipt",
                      "after-submit", "lost-claim", "lost-receipt", "lost-submit"]:
            case_root = root / phase
            case_root.mkdir()

            def run(action, expected):
                result = subprocess.run(
                    [str(binary), "--journal-process", action, str(case_root)],
                    capture_output=True, text=True, encoding="utf-8", timeout=20, check=False,
                )
                record = {"action": action, "exitCode": result.returncode,
                          "stdout": result.stdout, "stderr": result.stderr}
                assert result.returncode == expected, record
                return record

            first = run(phase, 42 if phase.startswith("lost-") else 86)
            journal = case_root / "journal"
            before = {p.name: digest(p) for p in journal.iterdir() if p.suffix in {".claim", ".receipt"}}
            if phase == "before-claim":
                assert not before, before
            else:
                assert sum(name.endswith(".claim") for name in before) == 1, before
            if phase in {"after-receipt", "after-submit", "lost-receipt", "lost-submit"}:
                assert sum(name.endswith(".receipt") for name in before) == 1, before
            recovery = run("recover", 0)
            outcome = json.loads(recovery["stdout"].splitlines()[-1])
            assert outcome["calls"] == (1 if phase == "before-claim" else 0), outcome
            expected_status = "unknown" if phase in {"after-claim", "after-handler", "lost-claim"} else "completed"
            expected_effects = 0 if phase in {"after-claim", "lost-claim"} else 1
            assert outcome["receipt"]["status"] == expected_status, outcome
            assert outcome["sideEffects"] == expected_effects, outcome
            replay = run("recover", 0)
            again = json.loads(replay["stdout"].splitlines()[-1])
            assert again["calls"] == 0 and again["sideEffects"] == expected_effects, again
            assert again["receipt"] == outcome["receipt"], (outcome, again)
            after = {p.name: digest(p) for p in journal.iterdir() if p.suffix in {".claim", ".receipt"}}
            assert all(after[name] == value for name, value in before.items()), (before, after)
            report["cases"].append({"phase": phase, "initial": first, "recovery": recovery,
                                    "replay": replay, "before": before, "after": after,
                                    "passed": True})
        report["passed"] = len(report["cases"]) == 8
    finally:
        assert root.parent == temp and root.name.startswith("tansr-cpp-executor-crash-")
        shutil.rmtree(root)
        report["cleaned"] = not root.exists()
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "cases": len(report["cases"]),
                      "cleaned": report["cleaned"]}))


if __name__ == "__main__":
    main()
