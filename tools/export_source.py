#!/usr/bin/env python3
"""从明确合同模式导出逐文件审查的公开源码；不携带Git历史或发布产物。"""
import argparse
import hashlib
import json
from pathlib import Path
import stat
import sys

sys.dont_write_bytecode = True
from contract_check import LOCK_HASH, PUBLIC_CANDIDATES, PUBLIC_METADATA, check, read_asset, strict_json
from contract_export import public_payloads
from generate_api import generate, verify

# 显式逐文件范围；新增文件须评审，不递归复制目录、不使用git archive泄露私有历史。
SOURCE_FILES = (
    '.clang-format',
    '.clang-tidy',
    '.gitattributes',
    '.github/workflows/ci.yml',
    '.gitignore',
    'CMakeLists.txt',
    'CMakePresets.json',
    'Doxyfile',
    'LICENSE',
    'README.en.md',
    'README.md',
    'cmake/TansrSDKConfig.cmake.in',
    'demo/CMakeLists.txt',
    'demo/archive.cpp',
    'demo/chat.cpp',
    'demo/common.cpp',
    'demo/common.hpp',
    'demo/tools.cpp',
    'demo/memory.cpp',
    'demo/memory_binding.hpp',
    'doc/guide.md',
    'doc/使用指南.md',
    'include/tansr/api.hpp',
    'include/tansr/archive.hpp',
    'include/tansr/cancellation.hpp',
    'include/tansr/canonical.hpp',
    'include/tansr/crypto.hpp',
    'include/tansr/error.hpp',
    'include/tansr/executor.hpp',
    'include/tansr/memory_publication.hpp',
    'include/tansr/terminal_persistence.hpp',
    'include/tansr/detail/terminal_persistence_digest.inc',
    'include/tansr/export.hpp',
    'include/tansr/json.hpp',
    'include/tansr/operations.hpp',
    'include/tansr/runtime.hpp',
    'include/tansr/sdk.hpp',
    'include/tansr/session.hpp',
    'include/tansr/sse.hpp',
    'include/tansr/storage.hpp',
    'integration/demo_chat_controls.py',
    'integration/demo_console.py',
    'integration/demo_create_loss.py',
    'integration/demo_delivery.py',
    'integration/demos.py',
    'integration/network.py',
    'integration/publication.py',
    'integration/run.py',
    'integration/sessions.py',
    'profiles/terminal-persistence-v1/LOCK.json',
    'profiles/terminal-persistence-v1/terminal-persistence-v1.schema.json',
    'profiles/terminal-persistence-v1/terminal-persistence-v1.golden.json',
    'packaging/NOTICE.md',
    'packaging/README.md',
    'packaging/RELEASE.md',
    'packaging/RIGHTS.txt',
    'packaging/conan/conandata.yml',
    'packaging/conan/conanfile.py',
    'packaging/conan/test_package/CMakeLists.txt',
    'packaging/conan/test_package/conanfile.py',
    'packaging/conan/test_package/main.cpp',
    'packaging/dependencies.json',
    'packaging/licenses/c-ares-1.34.8-LICENSE.md',
    'packaging/licenses/curl-8.22.0-COPYING',
    'packaging/licenses/openssl-3.5.9-LICENSE.txt',
    'packaging/vcpkg/tansr-sdk/portfile.cmake',
    'packaging/vcpkg/tansr-sdk/source.json',
    'packaging/vcpkg/tansr-sdk/vcpkg.json',
    'packaging/vcpkg/triplets/arm64-osx-tansr.cmake',
    'packaging/vcpkg/triplets/x64-linux-tansr.cmake',
    'packaging/vcpkg/triplets/x64-windows-tansr-static-md.cmake',
    'src/api/client.cpp',
    'src/api/operations.inc',
    'src/api/schema.cpp',
    'src/api/schema_data.inc',
    'src/archive/client.cpp',
    'src/archive/internal.hpp',
    'src/archive/material.cpp',
    'src/archive/store.cpp',
    'src/archive/sync.cpp',
    'src/archive/validation.cpp',
    'src/cancellation.cpp',
    'src/canonical.cpp',
    'src/crypto.cpp',
    'src/executor/client.cpp',
    'src/executor/internal.hpp',
    'src/executor/journal.cpp',
    'src/executor/output.cpp',
    'src/executor/runner.cpp',
    'src/executor/tool.cpp',
    'src/executor/types.cpp',
    'src/json.cpp',
    'src/memory_publication.cpp',
    'src/terminal_persistence.cpp',
    'src/terminal_persistence_state.inc',
    'src/runtime.cpp',
    'src/session/control.cpp',
    'src/session/events.cpp',
    'src/session/internal.hpp',
    'src/session/session.cpp',
    'src/sse.cpp',
    'src/storage/private_directory.cpp',
    'src/time.hpp',
    'tests/api_test.cpp',
    'tests/archive_adversarial_test.cpp',
    'tests/archive_test.cpp',
    'tests/archive_test_support.hpp',
    'tests/cancellation_test.cpp',
    'tests/canonical_test.cpp',
    'tests/consumer/CMakeLists.txt',
    'tests/consumer/main.cpp',
    'tests/contract_test.cpp',
    'tests/crypto_test.cpp',
    'tests/executor_test.cpp',
    'tests/integration/archive.cpp',
    'tests/integration/demo_seed.cpp',
    'tests/integration/executor.cpp',
    'tests/integration/executor_crash.py',
    'tests/integration/network.cpp',
    'tests/integration/publication.cpp',
    'tests/integration/persistence.inc',
    'tests/integration/session.cpp',
    'tests/json_test.cpp',
    'tests/memory_publication_test.cpp',
    'tests/terminal_persistence_test.cpp',
    'tests/memory_binding_test.cpp',
    'tests/memory_publication_migration_test.cpp',
    'tests/runtime_test.cpp',
    'tests/session_test.cpp',
    'tests/sse_test.cpp',
    'tests/storage_race_test.cpp',
    'tests/storage_test.cpp',
    'tests/tls_test.cpp',
    'tools/CI.md',
    'tools/check_format.py',
    'tools/ci.py',
    'tools/contract_check.py',
    'tools/contract_check_test.py',
    'tools/contract_export.py',
    'tools/export_source.py',
    'tools/export_source_test.py',
    'tools/generate_api.py',
    'tools/package_release.py',
    'tools/package_release_test.py',
    'tools/prepare_dependencies.py',
    'tools/prepare_dependencies_test.py',

)
MANIFEST = "SOURCE-MANIFEST.json"
PUBLIC_AGENTS = """# Tansr C++ SDK contributor guide

This repository contains the native C++17 SDK and public API demos under MIT.
Serve owns the agent core; this client must not silently execute tools on the Serve host.

- Preserve the frozen UAPI revision 7 contract bytes and generated operation/schema tables.
- This public distribution contains 20 approved contract assets. Explicitly use
  python tools/contract_check.py --mode public,
  python tools/generate_api.py --mode public --check, and
  python tools/contract_check_test.py --mode public.
  Never infer a different contract mode from missing files.
- Follow the CMake/CTest and tools/CI.md build and installation instructions.
  Integration drivers require an explicitly supplied compatible Serve fixture and
  its provenance; no private fixture or credentials are distributed here.
- Keep callback ownership, cancellation, authorization, durable recovery and tool
  output receipt boundaries intact. Add focused regression coverage for changes.
- Do not commit credentials, build outputs, runtime logs or user archives.
  Source and binary release steps are documented in packaging/RELEASE.md.
- A source export is a reviewed snapshot, not proof that a tag, package or public
  registry release has already been published.
"""
GENERATED_FILES = ("AGENTS.md", MANIFEST)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def expected_paths():
    return set(SOURCE_FILES) | {"contract/" + p for p in PUBLIC_CANDIDATES + PUBLIC_METADATA} | set(GENERATED_FILES)


def export_source(root, output, *, contract_mode="internal"):
    root, output = Path(root), Path(output)
    if output.resolve().is_relative_to(root.resolve()) or root.resolve().is_relative_to(output.resolve()):
        raise ValueError("public source destination must be outside the source tree")
    # 授权、全部输入及生成漂移在写输出前核完；缺件不能降级或产生貌似完整的发行树。
    contract = public_payloads(root / "contract", source_mode=contract_mode)
    verify(root, generate(root, mode=contract_mode))
    payloads = {name: read_asset(root, name) for name in SOURCE_FILES}
    if b"MIT License" not in payloads["LICENSE"]:
        raise ValueError("reviewed MIT LICENSE is required")
    payloads.update({"contract/" + name: raw for name, raw in contract.items()})
    payloads["AGENTS.md"] = PUBLIC_AGENTS.encode("utf-8")
    entries = [{"path": name, "bytes": len(raw), "sha256": digest(raw)} for name, raw in sorted(payloads.items())]
    manifest = {"format": "tansr-cpp-public-source-v1", "distribution": "public",
                "license": "MIT", "repository": "https://github.com/tansrai/tansr-cpp",
                "sourceLockSha256": LOCK_HASH, "files": entries}
    payloads[MANIFEST] = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    output.mkdir(exist_ok=False)
    for name, raw in sorted(payloads.items()):
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as stream:
            stream.write(raw)
    verify_export(output)
    return manifest


def verify_export(root):
    root = Path(root)
    allowed = expected_paths()
    parents = {str(parent).replace("\\", "/") for name in allowed
               for parent in Path(name).parents if str(parent) != "."}
    found = set()
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("links/reparse points are forbidden in public source")
        if path.is_dir():
            if relative not in parents:
                raise ValueError("unapproved public source directory: " + relative)
        elif path.is_file():
            found.add(relative)
        else:
            raise ValueError("non-regular public source entry: " + relative)
    if found != allowed:
        raise ValueError("public source inventory has missing or unexpected files")
    manifest = strict_json(read_asset(root, MANIFEST))
    if (set(manifest) != {"format", "distribution", "license", "repository", "sourceLockSha256", "files"}
            or manifest["format"] != "tansr-cpp-public-source-v1"
            or manifest["distribution"] != "public" or manifest["license"] != "MIT"
            or manifest["repository"] != "https://github.com/tansrai/tansr-cpp"
            or manifest["sourceLockSha256"] != LOCK_HASH):
        raise ValueError("public source manifest identity mismatch")
    entries = manifest["files"]
    if not isinstance(entries, list) or {item["path"] for item in entries} != allowed - {MANIFEST} or len(entries) != len(allowed) - 1:
        raise ValueError("public source manifest file inventory mismatch")
    for item in entries:
        raw = read_asset(root, item["path"])
        if set(item) != {"path", "bytes", "sha256"} or item["bytes"] != len(raw) or item["sha256"] != digest(raw):
            raise ValueError("public source bytes differ: " + item["path"])
    if read_asset(root, "AGENTS.md") != PUBLIC_AGENTS.encode("utf-8"):
        raise ValueError("public contributor guide differs from reviewed text")
    if b"MIT License" not in read_asset(root, "LICENSE"):
        raise ValueError("reviewed MIT LICENSE is required")
    check(root / "contract", mode="public")
    verify(root, generate(root, mode="public"))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--contract-mode", choices=("internal", "public"), default="internal")
    command = parser.add_mutually_exclusive_group(required=True)
    command.add_argument("--output", type=Path, help="源树外尚不存在的新目录")
    command.add_argument("--verify", action="store_true", help="检查已有公开导出树，不修改")
    args = parser.parse_args()
    if args.verify:
        if args.contract_mode != "public":
            parser.error("--verify requires explicit --contract-mode public")
        manifest = verify_export(args.root)
    else:
        manifest = export_source(args.root, args.output, contract_mode=args.contract_mode)
    print("public-source: " + str(len(manifest["files"])) +
          " reviewed files plus manifest; no Git history, upload or release performed")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError) as error:
        print("public-source failed: " + str(error), file=sys.stderr)
        sys.exit(1)
