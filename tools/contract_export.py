#!/usr/bin/env python3
"""按本库已批准策略导出公开合同；不提交、上传或发布。"""
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from contract_check import PUBLIC_CANDIDATES, PUBLIC_METADATA, check, check_policy, distribution, read_asset


def public_payloads(root, *, source_mode="internal"):
    root = Path(root)
    check(root, mode=source_mode)
    check_policy(root, require_approved=True)
    # 先读完并验证原件，授权/缺件/摘要错误不会留下半份输出。
    payloads = {name: read_asset(root, name) for name in PUBLIC_CANDIDATES + PUBLIC_METADATA
                if name != "DISTRIBUTION.json"}
    payloads["DISTRIBUTION.json"] = (json.dumps(distribution("public"), indent=2) + "\n").encode("utf-8")
    return payloads


def export_public(root, output, *, source_mode="internal"):
    root, output = Path(root), Path(output)
    payloads = public_payloads(root, source_mode=source_mode)
    if output.resolve().is_relative_to(root.resolve()) or root.resolve().is_relative_to(output.resolve()):
        raise ValueError("public export destination must be outside the source contract tree")
    output.mkdir(exist_ok=False)
    # 只创建新目录内允许的文件。写盘失败保留失败件供人工诊断，绝不覆盖或清理未知目录。
    for name, raw in payloads.items():
        with (output / name).open("xb") as stream:
            stream.write(raw)
    check(output, mode="public")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1] / "contract")
    parser.add_argument("--mode", choices=("public",), required=True)
    parser.add_argument("--source-mode", choices=("internal", "public"), default="internal",
                        help="源合同的明确模式；默认完整39项，不按缺件推断")
    parser.add_argument("--output", type=Path, required=True, help="必须是源树外尚不存在的新目录")
    args = parser.parse_args()
    export_public(args.root, args.output, source_mode=args.source_mode)
    print("contract-export: explicit public scope verified; publication is a separate authorized action")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError) as error:
        print("contract-export failed: " + str(error), file=sys.stderr)
        sys.exit(1)
