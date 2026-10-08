"""准备已安装原生 SDK/Demo 包；不构建、不联网、不签名、不上传。"""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEMOS = ('tansr-chat', 'tansr-tools', 'tansr-archive')
LICENSES = ('curl-8.22.0-COPYING', 'c-ares-1.34.8-LICENSE.md', 'openssl-3.5.9-LICENSE.txt')
BAD_PATH = re.compile(rb'(?:(?<![A-Za-z0-9_])[A-Za-z]:[\\/]|/(?:Users|home|workspace|workspaces)/)[^\x00\r\n\t "<>]{3,}')
FORBIDDEN = (b'-----BEGIN PRIVATE KEY-----', b'-----BEGIN RSA PRIVATE KEY-----',
             b'-----BEGIN OPENSSH PRIVATE KEY-----')
PUBLIC_SYSTEM_PATHS = (b'C:/Program Files/OpenSSL', b'C:/Program Files/Common Files/SSL')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode()


def no_links(path):
    path = Path(os.path.abspath(path))
    for parent in (path, *path.parents):
        if not parent.exists() and not parent.is_symlink():
            continue
        info = parent.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('symbolic link/reparse input or output is not allowed')
    return path


def safe_name(name):
    pieces = name.split('/')
    if (PurePosixPath(name).is_absolute() or '\\' in name or ':' in name
            or any(piece in ('', '.', '..') or piece.rstrip(' .') != piece for piece in pieces)
            or any(ord(char) < 32 for char in name)):
        raise ValueError('unsafe package-relative name')


def clean_bytes(data, label, forbidden_paths):
    # CodeView/字符串池同样检查；不能只移除 PDB 后宣称无构建机路径。
    for view in (data, data[::2] if b'\x00' in data else b'', data[1::2] if b'\x00' in data else b''):
        if any(value and value in view for value in forbidden_paths):
            raise ValueError('original private prefix in selected payload: ' + label)
        # OpenSSL's standard public runtime search locations are not build paths.
        for prefix in PUBLIC_SYSTEM_PATHS:
            view = view.replace(prefix, b'<system-openssl>').replace(prefix.replace(b'/', b'\\'), b'<system-openssl>')
        # These public guide examples deliberately use a generic placeholder;
        # never apply this exception to binaries, metadata or arbitrary files.
        if label in ('share/TansrSDK/doc/guide.md', 'share/TansrSDK/doc/使用指南.md'):
            view = view.replace(b'C:/absolute/', b'<absolute-path>/')
        if BAD_PATH.search(view) or any(value in view for value in FORBIDDEN):
            raise ValueError('private path or key marker in selected payload: ' + label)


def metadata(value):
    if set(value) != {'version', 'source', 'abi', 'signing', 'runtimeDependencies'}:
        raise ValueError('metadata requires version/source/abi/signing/runtimeDependencies only')
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[a-z0-9.-]+)?', value['version']):
        raise ValueError('invalid version')
    source = value['source']
    if (set(source) != {'repository', 'commit', 'tree', 'dirty', 'snapshotSha256'}
            or source['repository'] != 'https://github.com/tansrai/tansr-cpp' or source['dirty'] is not False
            or any(not isinstance(source[key], str) or not re.fullmatch(pattern, source[key])
                   for key, pattern in [('commit', '[0-9a-f]{40}'), ('tree', '[0-9a-f]{40}'), ('snapshotSha256', '[0-9a-f]{64}')])):
        raise ValueError('source must identify the immutable clean public snapshot')
    abi = value['abi']
    if set(abi) != {'os', 'arch', 'compiler', 'standardLibrary', 'runtime', 'configuration', 'linkage', 'minimumOs'}:
        raise ValueError('complete ABI metadata required')
    if (abi['os'], abi['arch']) not in {('windows', 'x64'), ('linux', 'x64'), ('macos', 'arm64')}:
        raise ValueError('unsupported native release target')
    if abi['configuration'] != 'Release' or abi['linkage'] != 'static':
        raise ValueError('this release entry packages verified Release static SDK/Demo products only')
    if any(not isinstance(item, str) or not item or len(item) > 128 or not re.fullmatch(r'[A-Za-z0-9_.+ ()/-]+', item) for item in abi.values()):
        raise ValueError('invalid ABI label')
    if abi['os'] == 'windows' and abi['runtime'] != 'MD':
        raise ValueError('Windows release dependencies and SDK require verified MD CRT')
    signing = value['signing']
    if (set(signing) != {'status', 'notarized'} or signing['notarized'] is not False
            or signing['status'] not in ('unsigned', 'ad-hoc')
            or signing['status'] == 'ad-hoc' and abi['os'] != 'macos'):
        raise ValueError('only explicit unsigned/local ad-hoc preparation is supported; no notarization claim')
    dependencies = value['runtimeDependencies']
    if (not isinstance(dependencies, list) or not dependencies or len(dependencies) > 32
            or any(not isinstance(item, str) or not re.fullmatch(r'[A-Za-z0-9_.+ ()-]{1,128}', item)
                   or any(word in item.lower() for word in ('node', 'python', 'rust', 'go runtime')) for item in dependencies)):
        raise ValueError('declare actual OS/CRT runtime dependencies, without paths or external language runtime')
    clean_bytes(json_bytes(value), 'release metadata', [])
    return value


def inspect_executable(data, os_name):
    if os_name == 'windows':
        if len(data) < 64 or data[:2] != b'MZ':
            raise ValueError('Demo is not a PE executable')
        offset = struct.unpack_from('<I', data, 60)[0]
        if data[offset:offset + 4] != b'PE\x00\x00' or data[offset + 4:offset + 6] != b'd\x86':
            raise ValueError('Demo is not Windows x64')
    elif os_name == 'linux':
        if len(data) < 20 or data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 62:
            raise ValueError('Demo is not Linux x64 ELF')
    elif len(data) < 8 or data[:4] != b'\xcf\xfa\xed\xfe' or struct.unpack_from('<I', data, 4)[0] != 0x100000c:
        raise ValueError('Demo is not macOS arm64 Mach-O')


class Payload:
    def __init__(self, forbidden_paths):
        self.files = {}
        self.origins = []
        self.forbidden_paths = forbidden_paths
        self.size = 0

    def put(self, name, data, executable=False, source=None, original=None):
        safe_name(name)
        if name in self.files or any(existing.casefold() == name.casefold() for existing in self.files):
            raise ValueError('duplicate/case-colliding package entry: ' + name)
        if len(data) > 128 * 1024 * 1024 or self.size + len(data) > 512 * 1024 * 1024 or len(self.files) >= 4096:
            raise ValueError('unexpected package file size')
        clean_bytes(data, name, self.forbidden_paths)
        self.files[name] = (data, executable)
        self.size += len(data)
        if source:
            self.origins.append({'packagePath': name, 'source': str(source),
                                 'sourceSha256': sha(original if original is not None else data),
                                 'packagedSha256': sha(data), 'transformed': original is not None and original != data})

    def read(self, source, name, executable=False, relocate_prefix=None):
        source = no_links(source)
        if not source.is_file():
            raise ValueError('required installed file missing: ' + str(source))
        original = source.read_bytes()
        data = original
        if relocate_prefix is not None:
            # curl exports a compatibility absolute link list. Only replace this exact
            # known prepared prefix; all remaining private paths still fail closed.
            replacement = b'${CMAKE_CURRENT_LIST_DIR}/../../..'
            for prefix in (str(relocate_prefix), str(relocate_prefix).replace('\\', '/')):
                data = data.replace(prefix.encode(), replacement)
        self.put(name, data, executable, source, original)
        if source.read_bytes() != original:
            raise ValueError('source changed during package preparation')

    def tree(self, source, destination, suffixes, relocate_prefix=None):
        source = no_links(source)
        if not source.is_dir():
            raise ValueError('required installed directory missing: ' + str(source))
        for parent, directories, files in os.walk(source, followlinks=False):
            for child in directories:
                no_links(Path(parent) / child)
            for child in files:
                path = no_links(Path(parent) / child)
                if path.suffix not in suffixes:
                    raise ValueError('unexpected file in selected install tree: ' + str(path))
                self.read(path, destination + '/' + path.relative_to(source).as_posix(), relocate_prefix=relocate_prefix)


def common_payload(install, forbidden_paths):
    payload = Payload(forbidden_paths)
    share = install / 'share/TansrSDK'
    for name in ('LICENSE', 'README.md', 'README.en.md', 'doc/使用指南.md', 'doc/guide.md',
                 'packaging/NOTICE.md', 'packaging/RIGHTS.txt', 'packaging/dependencies.json', 'packaging/README.md'):
        payload.read(share / name, 'share/TansrSDK/' + name)
    if b'MIT License' not in payload.files['share/TansrSDK/LICENSE'][0]:
        raise ValueError('authorized MIT license missing from installed package')
    for name in LICENSES:
        payload.read(share / 'packaging/licenses' / name, 'share/TansrSDK/packaging/licenses/' + name)
    return payload


def dependencies(payload, prefix, target):
    payload.tree(prefix / 'include/curl', 'dependencies/include/curl', {'.h'})
    payload.tree(prefix / 'include/openssl', 'dependencies/include/openssl', {'.h'})
    ares = sorted((prefix / 'include').glob('ares*.h'))
    if not ares:
        raise ValueError('c-ares headers missing')
    for path in ares:
        payload.read(path, 'dependencies/include/' + path.name)
    lib = 'lib64' if (prefix / 'lib64').is_dir() and not (prefix / 'lib').is_dir() else 'lib'
    names = ('libcurl.lib', 'cares.lib', 'libcrypto.lib', 'libssl.lib') if target == 'windows' else ('libcurl.a', 'libcares.a', 'libcrypto.a', 'libssl.a')
    for name in names:
        path = no_links(prefix / lib / name)
        if not path.read_bytes().startswith(b'!<arch>\n'):
            raise ValueError('dependency must be a real static archive: ' + name)
        payload.read(path, 'dependencies/' + lib + '/' + name)
    for name in ('CURL', 'c-ares', 'OpenSSL'):
        payload.tree(prefix / lib / 'cmake' / name, 'dependencies/' + lib + '/cmake/' + name, {'.cmake'}, relocate_prefix=prefix)


def write_package(output, name, payload, meta, kind, lock, lock_sha):
    files = [{'path': path, 'bytes': len(data), 'sha256': sha(data), 'executable': executable}
             for path, (data, executable) in sorted(payload.files.items())]
    manifest = {'format': 'tansr-cpp-binary-package-v1', 'kind': kind,
                **meta, 'dependencies': lock['dependencies'], 'dependencyLockSha256': lock_sha,
                'files': files, 'scope': 'Payload files only; manifest and SHA256SUMS exclude themselves.'}
    payload.put('manifest.json', json_bytes(manifest))
    sums = ''.join(row['sha256'] + '  ' + row['path'] + '\n' for row in files)
    payload.put('SHA256SUMS', sums.encode())
    # Include instructions in the hashed payload rather than adding an unlisted file.
    if 'USAGE.txt' not in payload.files:
        raise ValueError('package usage instructions were not finalized')
    extension = '.zip' if meta['abi']['os'] == 'windows' else '.tar.gz'
    archive = output / (name + extension)
    if extension == '.zip':
        with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as target:
            for path, (data, executable) in sorted(payload.files.items()):
                info = zipfile.ZipInfo(name + '/' + path, (2026, 1, 1, 0, 0, 0))
                info.create_system, info.external_attr = 3, (0o100755 if executable else 0o100644) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                target.writestr(info, data)
    else:
        with archive.open('xb') as stream, gzip.GzipFile(filename='', fileobj=stream, mode='wb', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w', format=tarfile.PAX_FORMAT) as target:
                for path, (data, executable) in sorted(payload.files.items()):
                    info = tarfile.TarInfo(name + '/' + path)
                    info.size, info.mode, info.mtime = len(data), 0o755 if executable else 0o644, 0
                    target.addfile(info, io.BytesIO(data))
    # Read the completed container back before it can be marked prepared.
    if extension == '.zip':
        with zipfile.ZipFile(archive) as packaged:
            actual = {entry.filename: packaged.read(entry) for entry in packaged.infolist()}
    else:
        with tarfile.open(archive, 'r:gz') as packaged:
            actual = {entry.name: packaged.extractfile(entry).read() for entry in packaged.getmembers()}
    expected = {name + '/' + path: data for path, (data, _) in payload.files.items()}
    if actual != expected:
        raise ValueError('archive readback differs from finalized payload')
    (output / (name + '.manifest.json')).write_bytes(json_bytes(manifest))
    return {'path': str(archive), 'bytes': archive.stat().st_size, 'sha256': sha(archive.read_bytes()),
            'kind': kind, 'manifestSha256': sha(json_bytes(manifest)), 'sourceFiles': payload.origins}


def prepare(install, dependency, output, meta, extra_forbidden=()):
    install, dependency, output = map(no_links, (install, dependency, output))
    if output.exists() or output == install or output == dependency or output.is_relative_to(install) or output.is_relative_to(dependency):
        raise ValueError('output must be a fresh directory outside installed inputs')
    meta = metadata(meta)
    forbidden = [str(path).encode() for path in (install, dependency, ROOT, *extra_forbidden)]
    forbidden += [value.replace(b'\\', b'/') for value in forbidden]
    output.mkdir(parents=True, exist_ok=False)
    receipt = {'status': 'failed', 'scope': 'local-release-preparation-only',
               'installPrefix': str(install), 'dependencyPrefix': str(dependency), 'packages': []}
    try:
        lock_raw = no_links(install / 'share/TansrSDK/packaging/dependencies.json').read_bytes()
        lock = json.loads(lock_raw)
        original_lock = json.loads((ROOT / 'packaging/dependencies.json').read_bytes())
        if lock != original_lock or [(item['name'], item['version']) for item in lock['dependencies']] != [('curl', '8.22.0'), ('c-ares', '1.34.8'), ('OpenSSL', '3.5.9')]:
            raise ValueError('installed dependency lock differs from selected release source')
        prefix = 'tansr-cpp-' + meta['version'] + '-' + meta['abi']['os'] + '-' + meta['abi']['arch'] + '-' + re.sub('[^a-z0-9.-]+', '-', meta['abi']['compiler'].lower()) + '-release-static'
        for kind in ('sdk', *DEMOS):
            payload = common_payload(install, forbidden)
            if kind == 'sdk':
                payload.tree(install / 'include/tansr', 'include/tansr', {'.hpp'})
                payload.tree(install / 'lib/cmake/TansrSDK', 'lib/cmake/TansrSDK', {'.cmake'})
                name = 'tansr_sdk.lib' if meta['abi']['os'] == 'windows' else 'libtansr_sdk.a'
                if not no_links(install / 'lib' / name).read_bytes().startswith(b'!<arch>\n'):
                    raise ValueError('SDK must be a real static archive')
                payload.read(install / 'lib' / name, 'lib/' + name)
                dependencies(payload, dependency, meta['abi']['os'])
            else:
                name = kind + ('.exe' if meta['abi']['os'] == 'windows' else '')
                binary = no_links(install / 'bin' / name)
                inspect_executable(binary.read_bytes(), meta['abi']['os'])
                payload.read(binary, 'bin/' + name, True)
            usage = ('Tansr C++ ' + kind + '\nSource: ' + meta['source']['repository'] + '\nCommit: ' + meta['source']['commit']
                + '\nABI: ' + json.dumps(meta['abi']) + '\nSigning: ' + meta['signing']['status'] + '; notarized: false\n'
                + 'Required OS/CRT: ' + ', '.join(meta['runtimeDependencies']) + '\n'
                + 'C++ ABI requires the same compiler/STL/CRT/configuration. No cross-toolchain binary ABI promise.\n'
                + ('CMAKE_PREFIX_PATH=<extracted-root>; find_package(TansrSDK CONFIG REQUIRED). Bundled dependencies are selected by the package config.\n' if kind == 'sdk'
                   else 'Run bin/' + name + ' --help. No external Node/Python runtime is required.\n')
                + 'See share/TansrSDK/doc/guide.md. Local release candidate; not a signed/public release receipt.\n')
            payload.put('USAGE.txt', usage.encode())
            receipt['packages'].append(write_package(output, prefix + '-' + kind, payload, meta, kind, lock, sha(lock_raw)))
        receipt['status'] = 'prepared'
        (output / 'SHA256SUMS').write_text(''.join(item['sha256'] + '  ' + Path(item['path']).name + '\n' for item in receipt['packages']), encoding='utf-8')
        return receipt
    except Exception as error:
        receipt['error'] = str(error)
        raise
    finally:
        (output / 'preparation-receipt.private.json').write_bytes(json_bytes(receipt))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('install-prefix', 'dependency-prefix', 'metadata', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--forbid-path', action='append', default=[])
    args = parser.parse_args()
    try:
        result = prepare(args.install_prefix, args.dependency_prefix, args.output,
                         json.loads(args.metadata.read_text(encoding='utf-8')), args.forbid_path)
        print(json.dumps({'status': result['status'], 'packages': [{key: item[key] for key in ('path', 'bytes', 'sha256')} for item in result['packages']]}))
        return 0
    except (ValueError, OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        parser.exit(1, 'package preparation failed: ' + str(error) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
