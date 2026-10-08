#!/usr/bin/env python3
"""公开源码边界回归；只操作OS临时树，不编译SDK或发布。"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import uuid

sys.dont_write_bytecode = True
from contract_check import PUBLIC_CANDIDATES, PUBLIC_METADATA
from export_source import GENERATED_FILES, MANIFEST, SOURCE_FILES, export_source, verify_export

TEST_MODE = "internal"


class SourceExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="tansr-cpp-source-export-")
        self.base = Path(self.temp.name)
        self.source = self.base / "source"
        self.source.mkdir()
        repository = Path(__file__).resolve().parents[1]
        shutil.copytree(repository / "contract", self.source / "contract")
        for name in SOURCE_FILES:
            target = self.source / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((repository / name).read_bytes())

    def tearDown(self):
        self.temp.cleanup()

    def export(self, name="public"):
        destination = self.base / name
        export_source(self.source, destination, contract_mode=TEST_MODE)
        return destination

    def test_exact_export_omits_history_private_material_and_unknown_files(self):
        # 合成敏感哨兵只在测试临时目录，检验白名单不会顺带复制未知内容。
        sentinel = ("SYNTHETIC-PRIVATE-" + uuid.uuid4().hex).encode("ascii")
        for name in (".git/config", "doc/开发进度.md", "integration/serve-fixture.mjs",
                     "archive/receipt.json", ".env", "src/unreviewed.cpp"):
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(sentinel)
        output = self.export()
        manifest = verify_export(output)
        expected = set(SOURCE_FILES) | {"contract/" + p for p in PUBLIC_CANDIDATES + PUBLIC_METADATA} | set(GENERATED_FILES)
        self.assertEqual({p.relative_to(output).as_posix() for p in output.rglob("*") if p.is_file()}, expected)
        self.assertNotIn(".git", {p.name for p in output.iterdir()})
        self.assertFalse((output / "contract/reference").exists())
        leaks = [p.relative_to(output).as_posix() for p in output.rglob("*")
                 if p.is_file() and sentinel in p.read_bytes()]
        self.assertEqual(leaks, [])
        self.assertEqual(len(manifest["files"]), len(expected) - 1)

    def test_public_reexport_and_generator_are_identical(self):
        first = self.export()
        second = self.base / "again"
        export_source(first, second, contract_mode="public")
        for path in first.rglob("*"):
            if path.is_file():
                self.assertEqual(path.read_bytes(), (second / path.relative_to(first)).read_bytes())
        with self.assertRaisesRegex(ValueError, "declaration"):
            export_source(first, self.base / "not-internal")
        self.assertFalse((self.base / "not-internal").exists())

    def test_missing_license_or_source_never_creates_output(self):
        for name in ("LICENSE", "src/api/client.cpp"):
            path = self.source / name
            original = path.read_bytes()
            path.unlink()
            with self.assertRaises(FileNotFoundError):
                self.export()
            self.assertFalse((self.base / "public").exists())
            path.write_bytes(original)

    def test_pending_policy_and_generated_drift_never_create_output(self):
        policy_path = self.source / "contract/EXPORT-POLICY.json"
        original = policy_path.read_bytes()
        policy = json.loads(original)
        policy.update(status="pending", authorization=None)
        policy_path.write_text(json.dumps(policy), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "not authorized"):
            self.export()
        self.assertFalse((self.base / "public").exists())
        policy_path.write_bytes(original)
        with (self.source / "src/api/operations.inc").open("ab") as stream:
            stream.write(b" drift")
        with self.assertRaisesRegex(ValueError, "generated output drift"):
            self.export()
        self.assertFalse((self.base / "public").exists())

    def test_existing_and_nested_destinations_are_preserved(self):
        existing = self.base / "existing"
        existing.mkdir()
        (existing / "user-file").write_bytes(b"unchanged")
        with self.assertRaises(FileExistsError):
            export_source(self.source, existing, contract_mode=TEST_MODE)
        self.assertEqual((existing / "user-file").read_bytes(), b"unchanged")
        for target in (self.source, self.source / "output", self.base):
            with self.assertRaisesRegex(ValueError, "outside"):
                export_source(self.source, target, contract_mode=TEST_MODE)

    def test_manifest_inventory_and_bytes_cannot_claim_false_completion(self):
        output = self.export()
        target = output / "README.md"
        target.write_bytes(target.read_bytes() + b" changed")
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            verify_export(output)
        (output / "unexpected.txt").write_bytes(b"not-approved")
        with self.assertRaisesRegex(ValueError, "inventory"):
            verify_export(output)
        (output / "unexpected.txt").unlink()
        original = json.loads((output / MANIFEST).read_bytes())
        original["files"].pop()
        (output / MANIFEST).write_text(json.dumps(original), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "manifest file inventory"):
            verify_export(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--mode", choices=("internal", "public"), default="internal")
    options, remaining = parser.parse_known_args()
    TEST_MODE = options.mode
    unittest.main(argv=[sys.argv[0], *remaining])
