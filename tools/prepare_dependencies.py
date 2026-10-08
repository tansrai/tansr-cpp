"""显式准备锁定依赖；默认只读本地源码缓存，普通 CMake 不调用本工具。"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import posixpath
import shutil
import stat
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def no_links(path):
    """在 resolve 前核所有已有祖先，拒绝 symlink、junction 和其他 reparse。"""
    path = Path(os.path.abspath(path))
    for item in (path, *path.parents):
        try:
            info = item.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('linked/reparse path is not an output/cache boundary: ' + str(item))
    return path


def fresh_output(path):
    path = no_links(path)
    if not path.is_relative_to(ROOT / 'out') or path == ROOT / 'out':
        raise ValueError('output must be a new child of this checkout/out')
    path.mkdir(parents=True, exist_ok=False)
    return path


def extract(archive, destination):
    """先验整棵路径图，再创建文件；包内文件链接展开为同字节文件。"""
    no_links(destination)
    if destination.exists():
        raise ValueError('extraction destination must not exist')
    with tarfile.open(archive, 'r:*') as source:
        entries = {}
        total = 0
        for entry in source:
            name = entry.name.rstrip('/')
            path = PurePosixPath(name)
            parts = name.split('/')
            reserved = {'con', 'prn', 'aux', 'nul', *(f'com{i}' for i in range(1, 10)), *(f'lpt{i}' for i in range(1, 10))}
            if (not name or path.is_absolute()
                    or any(p in ('.', '..', '') or p.rstrip(' .') != p or p.lower().split('.')[0] in reserved for p in parts)
                    or any(ord(char) < 32 for char in name)
                    or '\\' in name or ':' in name or name in entries
                    or not (entry.isdir() or entry.isfile() or entry.issym() or entry.islnk())):
                raise ValueError('unsafe/duplicate archive entry: ' + name)
            total += entry.size
            if len(entries) >= 100000 or total > 1024 * 1024 * 1024:
                raise ValueError('archive expansion exceeds explicit bound')
            entries[name] = entry
        roots = {PurePosixPath(name).parts[0] for name in entries}
        if len(roots) != 1:
            raise ValueError('source archive must have one root')
        for name in entries:
            for parent in PurePosixPath(name).parents:
                if str(parent) in entries and not entries[str(parent)].isdir():
                    raise ValueError('archive writes through non-directory ancestor')

        def regular(name, seen):
            if name in seen or name not in entries:
                raise ValueError('cyclic/missing archive link target')
            item = entries[name]
            if item.isfile():
                return item
            if not (item.issym() or item.islnk()):
                raise ValueError('only internal regular-file links are supported')
            link = item.linkname
            if PurePosixPath(link).is_absolute() or '\\' in link or ':' in link:
                raise ValueError('absolute archive link target')
            target = posixpath.normpath(posixpath.join(posixpath.dirname(name), link)
                                       if item.issym() else link)
            if target.split('/')[0] not in roots or target.startswith('../'):
                raise ValueError('archive link escapes source root')
            return regular(target, seen | {name})

        files = [(name, regular(name, set())) for name, item in entries.items() if not item.isdir()]
        destination.mkdir()
        for name, item in entries.items():
            if item.isdir():
                (destination / name).mkdir(parents=True, exist_ok=True)
        for name, item in files:
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            no_links(target)
            with source.extractfile(item) as reader, target.open('xb') as writer:
                shutil.copyfileobj(reader, writer)
            target.chmod(0o755 if item.mode & 0o111 else 0o644)
    return destination / next(iter(roots))


class Commands:
    def __init__(self, output, record):
        self.output, self.record = output, record
        (output / 'logs').mkdir()

    def run(self, label, command, cwd=None):
        command = [str(value) for value in command]
        print(label, flush=True)
        log = self.output / 'logs' / (label + '.log')
        with log.open('w', encoding='utf-8') as stream:
            result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)
        self.record.setdefault('commands', []).append({'label': label, 'arguments': command,
            'cwd': str(cwd or Path.cwd()), 'exitCode': result.returncode, 'log': str(log), 'sha256': sha256(log)})
        if result.returncode:
            raise RuntimeError(label + ' failed; see ' + str(log))


def sources(lock, cache, download):
    cache = no_links(cache)
    cache.mkdir(parents=True, exist_ok=True)
    # 文件名可沿已有缓存；身份只以锁定 SHA256 确认，不信任名字或 partial。
    indexed = {sha256(no_links(path)): path for path in cache.iterdir()
               if path.is_file() and not path.is_symlink() and path.name.endswith(('.tar.gz', '.tar.xz'))}
    found = {}
    for entry in lock['dependencies']:
        digest = entry['sha256']
        path = indexed.get(digest)
        if path is None:
            if not download:
                raise ValueError('locked cache missing: ' + entry['name'] + '; use --download explicitly')
            if not entry['source'].startswith('https://'):
                raise ValueError('dependency download requires locked HTTPS origin')
            path = cache / (entry['name'].lower() + '-' + entry['version'] + '-' + digest[:12] + '.tar.gz')
            no_links(path)
            with path.open('xb') as stream, urllib.request.urlopen(entry['source'], timeout=90) as response:
                if not response.url.startswith('https://'):
                    raise ValueError('non-HTTPS dependency redirect')
                shutil.copyfileobj(response, stream)
            if sha256(path) != digest:
                raise ValueError('download digest differs; failed cache retained: ' + str(path))
        no_links(path)
        found[entry['name']] = path
    if set(found) != {'curl', 'c-ares', 'OpenSSL'}:
        raise ValueError('unsupported dependency set; review preparation recipe')
    return found


def build(args, output, src, commands):
    system, machine = platform.system(), platform.machine().lower()
    targets = {('Windows', 'amd64'): 'VC-WIN64A', ('Linux', 'x86_64'): 'linux-x86_64',
               ('Linux', 'aarch64'): 'linux-aarch64', ('Darwin', 'arm64'): 'darwin64-arm64-cc',
               ('Darwin', 'x86_64'): 'darwin64-x86_64-cc'}
    if (system, machine) not in targets:
        raise ValueError('unsupported native target: ' + system + '/' + machine)
    prefix = output / 'prefix'
    prefix.mkdir()
    commands.run('cmake-version', [args.cmake, '--version'])
    commands.run('ninja-version', ['ninja', '--version'])
    commands.run('perl-version', [args.perl, '-V'])
    if system == 'Windows':
        if os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower() != 'x64':
            raise ValueError('enter an x64 MSVC developer environment first')
        os_name = subprocess.check_output([args.perl, '-e', 'print $^O'], text=True)
        if os_name != 'MSWin32':
            raise ValueError('OpenSSL requires native Windows Perl, not MSYS Perl')
    else:
        commands.run('compiler-version', [os.environ.get('CXX', 'c++'), '--version'])
    openssl = output / 'build-openssl'
    openssl.mkdir()
    configure = [args.perl, src['OpenSSL'] / 'Configure', targets[(system, machine)],
                 '--debug' if args.config == 'Debug' else '--release',
                 'no-shared', 'no-asm', 'no-tests', 'no-apps', 'no-docs', 'no-module', 'no-makedepend',
                 '--prefix=' + str(prefix), '--libdir=lib']
    configure += [('/MDd' if args.config == 'Debug' else '/MD')] if system == 'Windows' else ['-fPIC']
    commands.run('openssl-configure', configure, openssl)
    make = ['nmake', '/nologo'] if system == 'Windows' else ['make', '-j' + str(args.jobs)]
    commands.run('openssl-build', [*make, 'build_libs'], openssl)
    commands.run('openssl-install', [*make, 'install_dev'], openssl)
    common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=' + args.config,
              '-DCMAKE_INSTALL_PREFIX=' + str(prefix), '-DCMAKE_PREFIX_PATH=' + str(prefix),
              '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
              '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL']
    options = {'c-ares': ['-DCARES_STATIC=ON', '-DCARES_STATIC_PIC=ON', '-DCARES_SHARED=OFF',
                         '-DCARES_BUILD_TESTS=OFF', '-DCARES_BUILD_TOOLS=OFF'],
               'curl': ['-DBUILD_SHARED_LIBS=OFF', '-DBUILD_STATIC_LIBS=ON', '-DBUILD_CURL_EXE=OFF',
                        '-DBUILD_TESTING=OFF', '-DENABLE_ARES=ON', '-DHTTP_ONLY=ON',
                        '-DCURL_USE_LIBPSL=OFF', '-DCURL_USE_LIBSSH2=OFF', '-DCURL_ZLIB=OFF',
                        '-DCURL_BROTLI=OFF', '-DCURL_ZSTD=OFF', '-DUSE_NGHTTP2=OFF', '-DUSE_LIBIDN2=OFF']}
    if system == 'Windows':
        options['curl'] += ['-DCURL_USE_SCHANNEL=ON', '-DCURL_USE_OPENSSL=OFF']
    else:
        ca = args.ca_bundle or Path('/etc/ssl/cert.pem' if system == 'Darwin' else '/etc/ssl/certs/ca-certificates.crt')
        if not ca.is_file():
            raise ValueError('system CA bundle missing; provide --ca-bundle')
        options['curl'] += ['-DCURL_USE_OPENSSL=ON', '-DOPENSSL_USE_STATIC_LIBS=ON',
                            '-DOPENSSL_ROOT_DIR=' + str(prefix), '-DCURL_CA_BUNDLE=' + str(ca.resolve())]
    for name in ('c-ares', 'curl'):
        directory = output / ('build-' + name)
        commands.run(name + '-configure', [args.cmake, '-S', src[name], '-B', directory, *common, *options[name]])
        commands.run(name + '-build', [args.cmake, '--build', directory, '--parallel', args.jobs])
        commands.run(name + '-install', [args.cmake, '--install', directory])
    return prefix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--extract-only', action='store_true', help='只核摘要/安全解包，不声明已构建')
    parser.add_argument('--config', choices=('Release', 'Debug'), default='Release')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--perl', default='perl')
    parser.add_argument('--ca-bundle', type=Path)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error('--jobs must be 1..32')
    output = fresh_output(args.output)
    lock_path = ROOT / 'packaging/dependencies.json'
    record = {'status': 'failed', 'config': args.config, 'platform': platform.platform(),
              'machine': platform.machine(), 'python': sys.version, 'lockSha256': sha256(lock_path),
              'downloadEnabled': args.download, 'publicDistribution': False}
    try:
        lock = json.loads(lock_path.read_text(encoding='utf-8'))
        cached = sources(lock, args.cache, args.download)
        record['sources'] = [{'name': name, 'path': str(path), 'sha256': sha256(path)} for name, path in cached.items()]
        src = {name: extract(path, output / ('source-' + name.lower())) for name, path in cached.items()}
        if args.extract_only:
            record['status'] = 'extracted_only'
        else:
            prefix = build(args, output, src, Commands(output, record))
            record['installed'] = [{'path': str(path.relative_to(prefix)), 'sha256': sha256(path)}
                                   for path in sorted(prefix.rglob('*')) if path.is_file()]
            if not record['installed']:
                raise ValueError('empty dependency installation')
            record['status'], record['prefix'] = 'built', str(prefix)
        return 0
    except Exception as error:
        record['error'] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        (output / 'receipt.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    raise SystemExit(main())
