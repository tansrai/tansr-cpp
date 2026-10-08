"""同源本地/主线基础门；真实 Serve 只接受显式私有 fixture，不自动下载。"""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import xml.etree.ElementTree as ET

from prepare_dependencies import ROOT, Commands, fresh_output, sha256


def ctest(commands, executable, directory, label, record):
    report = commands.output / (label + '.xml')
    commands.run(label, [executable, '--test-dir', directory, '--output-on-failure',
                         '--no-tests=error', '--output-junit', report])
    cases = list(ET.parse(report).getroot().iter('testcase'))
    if not cases or any(case.find(tag) is not None for case in cases for tag in ('skipped', 'failure', 'error')):
        raise ValueError('empty/skipped/failed tests cannot pass: ' + label)
    record.setdefault('tests', []).append({'label': label, 'passed': len(cases), 'total': len(cases), 'skipped': 0})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--config', choices=('Release', 'Debug'), default='Release')
    parser.add_argument('--contract-mode', choices=('internal', 'public'), default='internal')
    parser.add_argument('--shared', action='store_true')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ctest', default='ctest')
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--provenance', type=Path)
    parser.add_argument('--node', type=Path)
    args = parser.parse_args()
    supplied = [args.fixture, args.provenance, args.node]
    if any(supplied) and not all(supplied):
        parser.error('--fixture, --provenance and --node must be supplied together')
    if all(supplied) and not all(path.is_file() for path in supplied):
        parser.error('explicit private fixture/provenance/Node input does not exist')
    if not 1 <= args.jobs <= 32:
        parser.error('--jobs must be 1..32')
    if not args.prefix.is_dir():
        parser.error('--prefix must be an existing prepared dependency installation')
    output, prefix = fresh_output(args.output), args.prefix.resolve()
    record = {'status': 'failed', 'platform': platform.platform(), 'python': sys.version,
              'config': args.config, 'shared': args.shared, 'contractMode': args.contract_mode, 'dependencyPrefix': str(prefix),
              'dependencyLockSha256': sha256(ROOT / 'packaging/dependencies.json'),
              'realServe': 'not_run', 'realServeReason': 'No private fixture input; not protocol acceptance.'}
    commands = Commands(output, record)
    try:
        # 源码归档没有 .git；不得误认其父目录仓库或伪称干净提交。
        record.update(commit=None, dirty=None, sourceKind='source-archive')
        try:
            git_root = subprocess.run(['git', 'rev-parse', '--show-toplevel'], cwd=ROOT,
                                      text=True, capture_output=True, check=False)
        except FileNotFoundError:
            git_root = None
        if git_root and git_root.returncode == 0 and Path(git_root.stdout.strip()).resolve() == ROOT.resolve():
            record['sourceKind'] = 'git-checkout'
            record['commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
            record['dirty'] = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True))
        if (ROOT / 'SOURCE-MANIFEST.json').is_file():
            record['sourceManifestSha256'] = sha256(ROOT / 'SOURCE-MANIFEST.json')
        commands.run('cmake-version', [args.cmake, '--version'])
        commands.run('contract', [sys.executable, ROOT / 'tools/contract_check.py', '--mode', args.contract_mode])
        commands.run('generated', [sys.executable, ROOT / 'tools/generate_api.py', '--check', '--mode', args.contract_mode])
        commands.run('preparation-guards', [sys.executable, ROOT / 'tools/prepare_dependencies_test.py'])
        build, install, consumer = output / 'build', output / 'install', output / 'consumer'
        flags = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=' + args.config, '-DOPENSSL_USE_STATIC_LIBS=ON',
                 '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL']
        commands.run('configure', [args.cmake, '-S', ROOT, '-B', build, *flags,
            '-DCMAKE_PREFIX_PATH=' + str(prefix), '-DCMAKE_INSTALL_PREFIX=' + str(install),
            '-DBUILD_SHARED_LIBS=' + ('ON' if args.shared else 'OFF'), '-DTANSR_BUILD_TESTS=ON',
            '-DTANSR_BUILD_DEMOS=ON', '-DTANSR_WARNINGS_AS_ERRORS=ON',
            '-DTANSR_BUILD_INTEGRATION=' + ('ON' if all(supplied) else 'OFF')])
        commands.run('build', [args.cmake, '--build', build, '--parallel', args.jobs])
        ctest(commands, args.ctest, build, 'unit', record)
        commands.run('install', [args.cmake, '--install', build])
        commands.run('consumer-configure', [args.cmake, '-S', ROOT / 'tests/consumer', '-B', consumer,
            *flags, '-DCMAKE_CXX_STANDARD=20', '-DCMAKE_PREFIX_PATH=' + str(install) + ';' + str(prefix)])
        commands.run('consumer-build', [args.cmake, '--build', consumer, '--parallel', args.jobs])
        # 动态 Windows 消费从已安装 DLL 运行，不能偷用开发树二进制。
        original_path = os.environ.get('PATH', '')
        try:
            os.environ['PATH'] = str(install / 'bin') + os.pathsep + original_path
            ctest(commands, args.ctest, consumer, 'installed-consumer', record)
        finally:
            os.environ['PATH'] = original_path
        if all(supplied):
            record['realServe'] = 'running'
            record.pop('realServeReason')
            inputs = ['--fixture', args.fixture.resolve(), '--provenance', args.provenance.resolve(),
                      '--node', args.node.resolve(), '--build', build]
            try:
                os.environ['PATH'] = str(build) + os.pathsep + original_path
                for driver in ('run', 'sessions', 'demos', 'demo_chat_controls', 'demo_delivery', 'demo_create_loss'):
                    commands.run('serve-' + driver, [sys.executable, ROOT / ('integration/' + driver + '.py'),
                        *inputs, '--logs', output / ('serve-' + driver)])
            finally:
                os.environ['PATH'] = original_path
            record['realServe'] = 'passed'
        record['status'] = 'passed'
        return 0
    except Exception as error:
        record['error'] = str(error)
        if record['realServe'] == 'running':
            record['realServe'] = 'failed'
        print(str(error), file=sys.stderr)
        return 1
    finally:
        (output / 'receipt.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print('realServe=' + record['realServe'] + '; scope=basic-build-and-installed-consumer')


if __name__ == '__main__':
    raise SystemExit(main())
