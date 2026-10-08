#!/usr/bin/env python3
"""研发合同检查：显式 internal/public，缺文件绝不推断发行模式。"""
import argparse
import hashlib
import json
from pathlib import Path
import stat
import subprocess
import sys

COMMIT = "83c64b2c519623a79994c942a5132300eb8174d4"
SCHEMA_HASH = "b60e77ffcbf08d985a993dbdbd4cf610f12f7c7e70f090aee5ff8d523f70bb57"
MANIFEST_HASH = "f6255bc868b4de5cb2af759a55069ab4651027d4125ef86d37f14a91d3307288"
FILES_HASH = "e4d377f8a4c00be3f27c94670b4bc389fc4ba6ae62eeece8178391fe28d8ca77"
LOCK_HASH = "1c761f0dbd1c1cc45229964739b48c8044fed37480cf98151fdfe2bf8f4746c0"

# 本库逐项审查的公开范围；许可来自本库 EXPORT-POLICY.json 的用户授权记录。
# 调用者不能传入另一份允许名单，也不能由原锁39项推断公开范围。
PUBLIC_CANDIDATES = (
    "api-error-map.json", "api-manifest.json", "archive-sync-v1.schema.json",
    "canonical-cross-vectors.json", "sdk2-archive-recovery-v1.golden.json",
    "sdk2-archive-recovery-v1.schema.json", "sdk2-cache-core-v1.schema.json",
    "sdk2-cache-v1.schema.json", "sdk2-ext-v1.schema.json", "sdk2-wire-v1.json",
    "terminal-observation-v1.golden.json", "terminal-observation-v1.schema.json",
    "terminal-profile-v1.golden.json", "terminal-profile-v1.schema.json",
    "terminal-services-v1.golden.json", "terminal-services-v1.schema.json",
    "terminal-shell-sandbox-v1.golden.json", "terminal-shell-sandbox-v1.schema.json",
    "unified-v1.golden.json", "unified-v1.schema.json",
)
PUBLIC_METADATA = ("LOCK.json", "PROVENANCE.json", "DISTRIBUTION.json", "EXPORT-POLICY.json", "README.md")


def strict_json(raw):
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate JSON key: " + key)
            result[key] = value
        return result
    return json.loads(raw, object_pairs_hook=unique_object)


def checked_path(root, relative):
    if (not isinstance(relative, str) or not relative or "\\" in relative or ":" in relative
            or any(ord(char) < 32 for char in relative)):
        raise ValueError("invalid contract relative path")
    if any(part in ("", "..", ".") for part in relative.split("/")):
        raise ValueError("contract path is not canonical")
    path = root / relative
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError("contract path escapes root")
    return path


def read_asset(root, relative):
    path = checked_path(root, relative)
    current = root
    for part in relative.split("/"):
        current = current / part
        attributes = current.lstat()
        if (stat.S_ISLNK(attributes.st_mode)
                or getattr(attributes, "st_file_attributes", 0) & 0x400):
            raise ValueError("contract links/reparse points are forbidden: " + relative)
    if not path.is_file():
        raise ValueError("contract asset is not a regular file: " + relative)
    return path.read_bytes()


def distribution(mode):
    return {"format": "tansr-cpp-contract-distribution-v1", "scope": mode,
            "sourceLockSha256": LOCK_HASH, "sourceLockFileCount": 39,
            "contractFileCount": 39 if mode == "internal" else len(PUBLIC_CANDIDATES)}


def check_policy(root, require_approved=False):
    policy = strict_json(read_asset(root, "EXPORT-POLICY.json"))
    if (set(policy) != {"format", "sourceCommit", "sourceLockSha256", "status", "files", "authorization"}
            or policy["format"] != "tansr-cpp-contract-export-policy-v1"
            or policy["sourceCommit"] != COMMIT or policy["sourceLockSha256"] != LOCK_HASH
            or policy["files"] != list(PUBLIC_CANDIDATES)):
        raise ValueError("export policy differs from the reviewed C++ candidate scope")
    if policy["status"] == "pending":
        if policy["authorization"] is not None:
            raise ValueError("pending export policy must not claim authorization")
        if require_approved:
            raise ValueError("C++ public contract export is not authorized: policy is pending")
    elif policy["status"] == "approved":
        if not isinstance(policy["authorization"], str) or not policy["authorization"].strip():
            raise ValueError("approved export policy requires the reviewed authorization record")
    else:
        raise ValueError("unknown export policy status")
    return policy


def check(root, source=None, *, mode="internal"):
    if mode not in ("internal", "public"):
        raise ValueError("contract mode must be explicit internal or public")
    root = Path(root)
    lock_raw = read_asset(root, "LOCK.json")
    lock = strict_json(lock_raw)
    expected = (COMMIT, 7, SCHEMA_HASH, 81, 11)
    actual = tuple(lock.get(k) for k in (
        "sourceCommit", "manifestRevision", "schemaHash", "operationCount", "familyCount"))
    if actual != expected or len(lock["files"]) != 39:
        raise ValueError("frozen baseline identity changed")
    pinned = json.dumps(lock["files"], sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode()
    if hashlib.sha256(pinned).hexdigest() != FILES_HASH:
        raise ValueError("frozen file identities changed")
    if hashlib.sha256(lock_raw).hexdigest() != LOCK_HASH:
        raise ValueError("frozen source lock bytes changed")
    if strict_json(read_asset(root, "DISTRIBUTION.json")) != distribution(mode):
        raise ValueError("distribution declaration does not match explicit " + mode + " mode")
    check_policy(root, require_approved=mode == "public")
    if mode == "public":
        expected_files = set(PUBLIC_CANDIDATES + PUBLIC_METADATA)
        if {path.name for path in root.iterdir()} != expected_files:
            raise ValueError("public contract inventory contains missing or unexpected entries")
    paths, sources = set(), set()
    for item in lock["files"]:
        path = checked_path(root, item["path"])
        if item["path"] in paths or item["source"] in sources:
            raise ValueError("duplicate asset path or source")
        paths.add(item["path"])
        sources.add(item["source"])
        if mode == "public" and item["path"] not in PUBLIC_CANDIDATES:
            continue
        raw = read_asset(root, item["path"])
        if hashlib.sha256(raw).hexdigest() != item["sha256"]:
            raise ValueError("asset digest mismatch: " + item["path"])
        if source is not None:
            checked_path(source, item["source"])
            original = subprocess.check_output(
                ["git", "show", COMMIT + ":" + item["source"]], cwd=source)
            if original != raw:
                raise ValueError("frozen Git object differs: " + item["path"])
    raw = read_asset(root, "api-manifest.json")
    if hashlib.sha256(raw).hexdigest() != MANIFEST_HASH:
        raise ValueError("frozen manifest digest changed")
    manifest = strict_json(raw)
    if len(manifest["operations"]) != 81 or len(manifest["families"]) != 11:
        raise ValueError("operation or family count changed")
    golden = strict_json(read_asset(root, "unified-v1.golden.json"))
    if len(golden["vectors"]) != 165:
        raise ValueError("unified golden count changed")
    provenance = strict_json(read_asset(root, "PROVENANCE.json"))
    if provenance["sourceCommit"] != COMMIT:
        raise ValueError("provenance differs from baseline")
    return lock


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1] / "contract")
    parser.add_argument("--source", type=Path, help="可选本地 CLI 检出；只读比较冻结 Git 原对象")
    parser.add_argument("--mode", choices=("internal", "public"), default="internal",
                        help="默认严格 internal；public 需显式选择及本库已批准策略，绝不自动降级")
    args = parser.parse_args()
    check(args.root, args.source, mode=args.mode)
    count = 39 if args.mode == "internal" else len(PUBLIC_CANDIDATES)
    print(f"contract-check: mode={args.mode}, {count}/{count} selected frozen assets, "
          "source lock=39, revision 7, 81 operations, 165 golden vectors")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print("contract-check failed: " + str(error), file=sys.stderr)
        sys.exit(1)
