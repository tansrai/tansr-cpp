#!/usr/bin/env python3
"""由冻结资产生成 C++ 内嵌表；普通 CMake/安装消费者不调用此工具。"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from contract_check import check


def literal(value):
    return json.dumps(value, ensure_ascii=True)


def optional(value):
    return "std::nullopt" if value is None else "std::string_view{" + literal(value) + "}"


def sequence(values):
    return "{" + ", ".join(literal(value) for value in values or []) + "}"


def generate(root, *, mode="internal"):
    check(root / "contract", mode=mode)
    manifest = json.loads((root / "contract/api-manifest.json").read_bytes())
    families = {item["id"]: item for item in manifest["families"]}
    unfenced = {"discovery.manifest", "discovery.capabilities", "discovery.session.capabilities", "session.capabilities"}
    lines = ["// 由 tools/generate_api.py 从冻结 manifest 生成，请勿手工修改。", "static const std::vector<Operation> operation_table{"]
    for operation in manifest["operations"]:
        expected = operation["expectedRevision"]
        revision = "std::nullopt" if expected is None else (
            "ExpectedRevision{" + sequence(expected["path"]) + ", " + literal(expected["kind"]) + "}")
        request_id = families.get(operation["family"], {}).get("requestIdPath")
        fields = [literal(operation["name"]), literal(operation["domain"]), optional(operation["family"]),
                  literal(operation["method"]), literal(operation["apiPath"]), literal(operation["kind"]),
                  str(operation["sse"]).lower(), str(operation["name"] not in unfenced).lower(),
                  optional(operation["request"]), optional(operation["response"]), sequence(operation["query"]),
                  sequence(request_id), sequence(operation["etagPath"]), revision]
        lines.append("    {" + ", ".join(fields) + "},")
    lines.append("};")
    outputs = {"src/api/operations.inc": "\n".join(lines) + "\n"}
    lines = ["// 由 tools/generate_api.py 从冻结 schema 原字节生成，请勿手工修改。"]
    entries = []
    profile = root / "profiles/terminal-persistence-v1"
    lock = json.loads((profile / "LOCK.json").read_bytes())
    if lock["definitionDigest"] != "33029a264edf81f3fda2a13fc382403d0cd7ffefa1f38088bb9366f387a13587":
        raise ValueError("terminal persistence definition digest drift")
    expected_assets = {
        "terminal-persistence-v1.schema.json": "47387fb03308d00244e876a3bb429e24b81ac8d94d5f611aeda43e03b7c8b16c",
        "terminal-persistence-v1.golden.json": "4b2c492ae590fda9447a5a53d13f3f7a935956c929cc23441a765947e3ef6503",
    }
    if lock["files"] != expected_assets or lock["profile"] != "terminal-persistence-v1" or lock["revision"] != "2026-10-10.v1":
        raise ValueError("terminal persistence profile lock drift")
    for name, digest in lock["files"].items():
        if hashlib.sha256((profile / name).read_bytes()).hexdigest() != digest:
            raise ValueError("terminal persistence profile asset drift: " + name)
    outputs["include/tansr/detail/terminal_persistence_digest.inc"] = (
        'inline constexpr const char *tool_digest = "' + lock["definitionDigest"] + '";\n')
    schemas = sorted((root / "contract").glob("*.schema.json")) + [profile / "terminal-persistence-v1.schema.json"]
    for index, path in enumerate(schemas):
        data = path.read_bytes().decode("utf-8")
        delimiter = "TANSR_SCHEMA"
        if ")" + delimiter + '"' in data:
            raise ValueError("raw string delimiter collision")
        # 单个字面量保持在 MSVC 上限内，不依赖巨型相邻字面量拼接。
        chunks = [data[start:start + 1024] for start in range(0, len(data), 1024)]
        array = "schema_chunks_" + str(index)
        lines.append("static constexpr std::string_view " + array + "[]{")
        for chunk in chunks:
            lines.append('    R"' + delimiter + "(" + chunk + ")" + delimiter + '",')
        lines.append("};")
        entries.append("    {" + literal(path.name.removesuffix(".schema.json")) + ", " + array + ", " + str(len(chunks)) + "},")
    lines.append("static constexpr EmbeddedSchema embedded_schemas[]{")
    lines.extend(entries)
    lines.append("};")
    outputs["src/api/schema_data.inc"] = "\n".join(lines) + "\n"
    return outputs


def verify(root, outputs):
    for relative, content in outputs.items():
        path = root / relative
        if not path.is_file() or path.read_bytes() != content.encode("utf-8"):
            raise ValueError("generated output drift: " + relative)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--mode", choices=("internal", "public"), default="internal",
                        help="明确合同消费模式；缺件不会从internal降级public")
    args = parser.parse_args()
    outputs = generate(args.root, mode=args.mode)
    if args.check:
        verify(args.root, outputs)
    else:
        for relative, content in outputs.items():
            path = args.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content.encode("utf-8"))
    print("generate-api: 81 operations and 11 embedded schemas " + ("verified" if args.check else "written"))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        print("generate-api failed: " + str(error), file=sys.stderr)
        sys.exit(1)
