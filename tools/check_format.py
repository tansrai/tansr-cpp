"""Check owned C++ sources with an explicitly selected clang-format binary."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang-format", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    paths = sorted(p for folder in ("include", "src", "tests", "demo", "packaging/conan/test_package")
                   for p in (root / folder).rglob("*") if p.suffix in (".cpp", ".hpp"))
    if not paths:
        raise RuntimeError("no C++ sources selected")
    failed = []
    for path in paths:
        result = subprocess.run([args.clang_format, "--dry-run", "--Werror", str(path)], cwd=root)
        if result.returncode:
            failed.append(str(path.relative_to(root)))
    print(f"clang-format: {len(paths)} files, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
