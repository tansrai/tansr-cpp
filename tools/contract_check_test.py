#!/usr/bin/env python3
"""冻结/生成检查器的紧凑负例；不启动产品测试池。"""
import argparse
import json
from pathlib import Path
import shutil
import tempfile
import unittest
import sys
sys.dont_write_bytecode = True
from contract_check import PUBLIC_CANDIDATES, PUBLIC_METADATA, check, checked_path, distribution
from contract_export import export_public
from generate_api import generate, verify

TEST_MODE = "internal"


class ContractCheckerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="tansr-cpp-contract-")
        self.root = Path(self.temp.name)
        shutil.copytree(Path(__file__).resolve().parents[1] / "contract", self.root / "contract")

    def tearDown(self):
        self.temp.cleanup()

    def write_json(self, name, value, root=None):
        ((root or self.root / "contract") / name).write_text(
            json.dumps(value, indent=2) + "\n", encoding="utf-8")

    def approve_fixture(self):
        # 仅私有 OS 临时夹具；不修改仓内策略，不构成公开授权。
        path = self.root / "contract/EXPORT-POLICY.json"
        policy = json.loads(path.read_bytes())
        policy["status"] = "approved"
        policy["authorization"] = "SYNTHETIC-TEST-ONLY: not a publication authorization"
        self.write_json("EXPORT-POLICY.json", policy)

    def public_fixture(self):
        self.approve_fixture()
        output = self.root / "public-fixture"
        export_public(self.root / "contract", output, source_mode=TEST_MODE)
        return output

    def test_original_and_generation_are_deterministic(self):
        self.assertEqual(len(check(self.root / "contract", mode=TEST_MODE)["files"]), 39)
        self.assertEqual(generate(self.root, mode=TEST_MODE), generate(self.root, mode=TEST_MODE))

    def test_missing_and_tampered_asset_are_rejected(self):
        path = self.root / "contract/unified-v1.schema.json"
        path.write_bytes(path.read_bytes() + b" ")
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            check(self.root / "contract", mode=TEST_MODE)
        path.unlink()
        with self.assertRaises((FileNotFoundError, ValueError)):
            check(self.root / "contract", mode=TEST_MODE)

    def test_generated_missing_and_drift_are_rejected(self):
        outputs = generate(self.root, mode=TEST_MODE)
        with self.assertRaisesRegex(ValueError, "generated output drift"):
            verify(self.root, outputs)
        for relative, content in outputs.items():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content.encode("utf-8"))
        verify(self.root, outputs)
        with (self.root / "src/api/operations.inc").open("ab") as file:
            file.write(b" ")
        with self.assertRaisesRegex(ValueError, "generated output drift"):
            verify(self.root, outputs)

    def test_lock_cannot_relabel_or_remove_frozen_assets(self):
        path = self.root / "contract/LOCK.json"
        lock = json.loads(path.read_bytes())
        lock["files"][0]["path"] = "../outside"
        path.write_text(json.dumps(lock), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "identities changed"):
            check(self.root / "contract", mode=TEST_MODE)
        lock["files"].pop()
        path.write_text(json.dumps(lock), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "baseline identity changed"):
            check(self.root / "contract", mode=TEST_MODE)

    def test_pending_policy_never_exports_and_modes_are_explicit(self):
        policy = json.loads((self.root / "contract/EXPORT-POLICY.json").read_bytes())
        self.write_json("EXPORT-POLICY.json", dict(policy, status="pending", authorization=None))
        output = self.root / "must-not-exist"
        with self.assertRaisesRegex(ValueError, "not authorized"):
            export_public(self.root / "contract", output, source_mode=TEST_MODE)
        self.assertFalse(output.exists())
        if TEST_MODE == "internal":
            with self.assertRaisesRegex(ValueError, "declaration"):
                check(self.root / "contract", mode="public")
        self.write_json("DISTRIBUTION.json", distribution("public"))
        with self.assertRaisesRegex(ValueError, "not authorized"):
            check(self.root / "contract", mode="public")
        with self.assertRaisesRegex(ValueError, "declaration"):
            check(self.root / "contract")
        with self.assertRaisesRegex(ValueError, "explicit"):
            check(self.root / "contract", mode="auto")

    def test_approved_fixture_exports_only_exact_public_files(self):
        output = self.public_fixture()
        self.assertEqual({item.name for item in output.iterdir()}, set(PUBLIC_CANDIDATES + PUBLIC_METADATA))
        self.assertFalse((output / "reference").exists())
        self.assertEqual(len(check(output, mode="public")["files"]), 39)
        for name in PUBLIC_CANDIDATES + ("LOCK.json",):
            self.assertEqual((output / name).read_bytes(), (self.root / "contract" / name).read_bytes())
        with self.assertRaisesRegex(ValueError, "declaration"):
            check(output)  # 不凭仅有 20 份文件自动选择 public。

    def test_public_missing_tampered_extra_and_private_entries_are_rejected(self):
        output = self.public_fixture()
        victim = output / "unified-v1.schema.json"
        original = victim.read_bytes()
        victim.write_bytes(original + b" ")
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            check(output, mode="public")
        victim.unlink()
        with self.assertRaisesRegex(ValueError, "inventory"):
            check(output, mode="public")
        # 同样条数但换名也不算允许范围。
        wrong = output / "unapproved.json"
        wrong.write_bytes(original)
        with self.assertRaisesRegex(ValueError, "inventory"):
            check(output, mode="public")
        wrong.unlink()
        victim.write_bytes(original)
        (output / "reference").mkdir()
        with self.assertRaisesRegex(ValueError, "inventory"):
            check(output, mode="public")

    def test_policy_cannot_replace_allowlist_or_claim_approval_without_record(self):
        path = self.root / "contract/EXPORT-POLICY.json"
        original = json.loads(path.read_bytes())
        for replacement in ("reference/private.ts", "unapproved.json", "../outside"):
            policy = dict(original, files=list(PUBLIC_CANDIDATES))
            policy["files"][0] = replacement  # 条数不变。
            self.write_json("EXPORT-POLICY.json", policy)
            with self.assertRaisesRegex(ValueError, r"reviewed C\+\+ candidate scope"):
                check(self.root / "contract", mode=TEST_MODE)
        self.write_json("EXPORT-POLICY.json", dict(original, status="approved", authorization=None))
        with self.assertRaisesRegex(ValueError, "authorization record"):
            check(self.root / "contract", mode=TEST_MODE)
        self.write_json("EXPORT-POLICY.json", dict(original, status="inherited-from-rust"))
        with self.assertRaisesRegex(ValueError, "unknown export policy"):
            check(self.root / "contract", mode=TEST_MODE)

    def test_internal_missing_private_asset_cannot_downgrade(self):
        lock = check(self.root / "contract", mode=TEST_MODE)
        private = next(item["path"] for item in lock["files"] if item["path"].startswith("reference/"))
        if TEST_MODE == "internal":
            (self.root / "contract" / private).unlink()
        else:
            self.assertFalse((self.root / "contract" / private).exists())
        with self.assertRaises((FileNotFoundError, ValueError)):
            check(self.root / "contract", mode="internal")
        with self.assertRaises((FileNotFoundError, ValueError)):
            export_public(self.root / "contract", self.root / "must-not-exist")
        self.assertFalse((self.root / "must-not-exist").exists())

    def test_export_does_not_overwrite_or_write_inside_source(self):
        self.approve_fixture()
        for destination in (self.root / "contract/export", self.root / "contract", self.root):
            with self.assertRaisesRegex(ValueError, "outside"):
                export_public(self.root / "contract", destination, source_mode=TEST_MODE)
        existing = self.root / "existing"
        existing.mkdir()
        sentinel = existing / "user-owned.txt"
        sentinel.write_bytes(b"preserve")
        with self.assertRaises(FileExistsError):
            export_public(self.root / "contract", existing, source_mode=TEST_MODE)
        self.assertEqual(sentinel.read_bytes(), b"preserve")

    def test_duplicate_metadata_and_noncanonical_paths_are_rejected(self):
        path = self.root / "contract/DISTRIBUTION.json"
        raw = path.read_text(encoding="utf-8")
        path.write_text(raw.replace('"scope":', '"scope": "invalid", "scope":', 1),
                        encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
            check(self.root / "contract", mode=TEST_MODE)
        for relative in ("../outside", "./file", "/absolute", "a//b", "C:/outside", "a\\b", "x\x00y"):
            with self.assertRaises(ValueError):
                checked_path(self.root / "contract", relative)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--mode", choices=("internal", "public"), default="internal")
    parsed, remaining = parser.parse_known_args()
    TEST_MODE = parsed.mode
    unittest.main(argv=[sys.argv[0], *remaining])
