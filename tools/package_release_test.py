"""发行容器/白名单工装测试；合成二进制不替代原生包消费验收。"""
import copy
import json
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest
import zipfile

import package_release as release


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='tansr-release-tool-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.install = self.root / 'installed'
        self.deps = self.root / 'deps'

    def put(self, root, path, data):
        dest = root / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data if isinstance(data, bytes) else data.encode())

    def fixture(self, os_name='windows'):
        meta = {
            'version': '0.1.0',
            'source': {'repository': 'https://github.com/tansrai/tansr-cpp',
                       'commit': '1' * 40, 'tree': '2' * 40, 'dirty': False,
                       'snapshotSha256': '3' * 64},
            'abi': {'os': os_name, 'arch': 'arm64' if os_name == 'macos' else 'x64',
                    'compiler': 'synthetic-test', 'standardLibrary': 'fixture',
                    'runtime': 'MD' if os_name == 'windows' else 'libc',
                    'configuration': 'Release', 'linkage': 'static', 'minimumOs': 'test-only'},
            'signing': {'status': 'unsigned', 'notarized': False},
            'runtimeDependencies': ['test-OS']}
        self.put(self.install, 'include/tansr/sdk.hpp', '// synthetic header\n')
        self.put(self.install, 'lib/cmake/TansrSDK/TansrSDKConfig.cmake', '# synthetic config\n')
        self.put(self.install, 'lib/' + ('tansr_sdk.lib' if os_name == 'windows' else 'libtansr_sdk.a'), b'!<arch>\nfixture')
        for name in ('LICENSE', 'README.md', 'README.en.md', 'doc/使用指南.md', 'doc/guide.md',
                     'packaging/NOTICE.md', 'packaging/RIGHTS.txt', 'packaging/README.md'):
            self.put(self.install, 'share/TansrSDK/' + name, 'MIT License\nSynthetic test fixture\n')
        self.put(self.install, 'share/TansrSDK/packaging/dependencies.json', (release.ROOT / 'packaging/dependencies.json').read_bytes())
        for name in release.LICENSES:
            self.put(self.install, 'share/TansrSDK/packaging/licenses/' + name, 'Synthetic license fixture\n')
        for name in ('curl/curl.h', 'openssl/ssl.h', 'ares.h'):
            self.put(self.deps, 'include/' + name, '// synthetic dependency\n')
        names = ('libcurl.lib', 'cares.lib', 'libcrypto.lib', 'libssl.lib') if os_name == 'windows' else ('libcurl.a', 'libcares.a', 'libcrypto.a', 'libssl.a')
        for name in names:
            self.put(self.deps, 'lib/' + name, b'!<arch>\nfixture')
        for name in ('CURL', 'c-ares', 'OpenSSL'):
            self.put(self.deps, 'lib/cmake/' + name + '/' + name + 'Config.cmake',
                     'set(TEST_PATH "' + self.deps.as_posix() + '/lib/fixture")\n')
        if os_name == 'windows':
            binary = bytearray(128)
            binary[:2] = b'MZ'
            struct.pack_into('<I', binary, 60, 64)
            binary[64:70] = b'PE\0\0\x64\x86'
        elif os_name == 'linux':
            binary = bytearray(64)
            binary[:6] = b'\x7fELF\x02\x01'
            struct.pack_into('<H', binary, 18, 62)
        else:
            binary = bytearray(b'\xcf\xfa\xed\xfe\x0c\0\0\x01' + b'\0' * 24)
        for name in release.DEMOS:
            self.put(self.install, 'bin/' + name + ('.exe' if os_name == 'windows' else ''), bytes(binary))
        # Neither a convenient prefix nor whole-repository glob is a release allowlist.
        self.put(self.install, 'share/contract/private.json', 'not public')
        self.put(self.install, 'bin/secret.env', 'not public')
        self.put(self.deps, 'lib/debug.pdb', 'not public')
        return meta

    def unpack(self, archive):
        if archive.suffix == '.zip':
            with zipfile.ZipFile(archive) as pack:
                return {name.split('/', 1)[1]: pack.read(name) for name in pack.namelist()}
        with tarfile.open(archive) as pack:
            return {entry.name.split('/', 1)[1]: pack.extractfile(entry).read() for entry in pack.getmembers()}

    def test_three_container_types_hashes_allowlist_and_determinism(self):
        for os_name in ('windows', 'linux', 'macos'):
            with self.subTest(os=os_name):
                self.install = self.root / os_name / 'install'
                self.deps = self.root / os_name / 'deps'
                meta = self.fixture(os_name)
                first = release.prepare(self.install, self.deps, self.root / (os_name + '-one'), meta)
                second = release.prepare(self.install, self.deps, self.root / (os_name + '-two'), meta)
                self.assertEqual(first['status'], 'prepared')
                self.assertEqual(len(first['packages']), 4)
                self.assertEqual([row['sha256'] for row in first['packages']], [row['sha256'] for row in second['packages']])
                for row in first['packages']:
                    files = self.unpack(Path(row['path']))
                    manifest = json.loads(files['manifest.json'])
                    expected = {'manifest.json', 'SHA256SUMS'} | {item['path'] for item in manifest['files']}
                    self.assertEqual(set(files), expected)
                    self.assertNotIn(b'not public', b''.join(files.values()))
                    self.assertEqual(manifest['dependencyLockSha256'], release.sha(files['share/TansrSDK/packaging/dependencies.json']))
                    for item in manifest['files']:
                        self.assertEqual(item['sha256'], release.sha(files[item['path']]))
                    if row['kind'] == 'sdk':
                        self.assertIn(b'${CMAKE_CURRENT_LIST_DIR}/../../..', files['dependencies/lib/cmake/CURL/CURLConfig.cmake'])
                        self.assertFalse(any(path.startswith('bin/') for path in files))
                    else:
                        self.assertEqual(sum(path.startswith('bin/') for path in files), 1)
                        self.assertFalse(any(path.startswith('dependencies/') for path in files))

    def test_metadata_rejects_dirty_wrong_crt_unknown_target_and_signing_claims(self):
        meta = self.fixture()
        for section, key, value in [('source', 'dirty', True), ('abi', 'runtime', 'MT'),
                                    ('abi', 'os', 'android'), ('signing', 'notarized', True),
                                    ('signing', 'status', 'signed')]:
            with self.subTest(key=key):
                invalid = copy.deepcopy(meta)
                invalid[section][key] = value
                with self.assertRaises(ValueError):
                    release.metadata(invalid)

    def test_payload_rejects_private_paths_and_keys_including_utf16(self):
        for content in (b'J:\\tansr\\private\\source.cpp', b'J:\\\\tansr\\\\private\\\\source.cpp', b'/Users/private/source.cpp',
                        'J:/tansr/private.cpp'.encode('utf-16le'), b'-----BEGIN PRIVATE KEY-----'):
            with self.subTest(content=content):
                with self.assertRaises(ValueError):
                    release.Payload([]).put('file.lib', content)
        with self.assertRaises(ValueError):
            release.Payload([b'custom-private-prefix']).put('file.hpp', b'custom-private-prefix/file')
        release.Payload([]).put('runtime.lib', b'C:/Program Files/OpenSSL\0C:\\Program Files\\Common Files\\SSL')
        release.Payload([]).put('share/TansrSDK/doc/guide.md', b'C:/absolute/demo.exe')
        with self.assertRaises(ValueError):
            release.Payload([]).put('demo.exe', b'C:/absolute/demo.exe')

    def test_names_and_case_collisions(self):
        for name in ('../escape', '/absolute', 'C:/path', 'folder\\file', 'foo/../bar', 'foo./bar'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                release.Payload([]).put(name, b'')
        payload = release.Payload([])
        payload.put('A.hpp', b'')
        with self.assertRaises(ValueError):
            payload.put('a.hpp', b'')

    def test_real_packaging_readme_placeholder_only_in_document(self):
        actual = (release.ROOT / 'packaging/README.md').read_bytes()
        self.assertIn(b'C:/absolute/', actual)
        payload = release.Payload([])
        payload.put('share/TansrSDK/packaging/README.md', actual)
        with self.assertRaises(ValueError):
            payload.put('bin/demo.exe', actual)
        with self.assertRaises(ValueError):
            release.Payload([]).put('share/TansrSDK/packaging/README.md', actual + b'\nJ:/tansr/private/source.cpp')

    def test_all_real_installed_documents_and_relative_scope_are_allowed(self):
        payload = release.Payload([])
        for name in ('LICENSE', 'README.md', 'README.en.md', 'doc/使用指南.md', 'doc/guide.md',
                     'packaging/NOTICE.md', 'packaging/RIGHTS.txt', 'packaging/dependencies.json', 'packaging/README.md'):
            payload.read(release.ROOT / name, 'share/TansrSDK/' + name)
        for name in release.LICENSES:
            payload.read(release.ROOT / 'packaging/licenses' / name, 'share/TansrSDK/packaging/licenses/' + name)
        release.Payload([]).put('scope.txt', b'user/app/workspace/tool')
        for actual in (b'/workspace/private/file', b'file=/workspace/private/file', b'\0/home/user/private/file'):
            with self.subTest(path=actual), self.assertRaises(ValueError):
                release.Payload([]).put('binary.lib', actual)

    def test_binary_url_formats_and_non_utf16_bytes_are_not_private_paths(self):
        payload = release.Payload([])
        payload.put('curl.lib', b'\0%s://%s\0%s://%s:%s/%s\0')
        # Bytes from different machine-code positions must not be interpreted as UTF-16.
        payload.put('crypto.lib', b'\x81I\x82:\x83/\x84a\x85b\x86c\x87d')
        payload.put('machine-code.lib', b'T:\\\xbeW\xa65;\x0c\xd4\0X:/?\x13_random\0')
        with self.assertRaises(ValueError):
            release.Payload([]).put('unicode-path.lib', 'J:/私有/private.cpp'.encode())
        for text in ('J:/tansr/private.cpp', '/Users/private/build/file.cpp'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                release.Payload([]).put('real-utf16.lib', b'\x81' + text.encode('utf-16le') + b'\0\0')

    def test_numeric_table_is_not_a_path_but_exact_private_prefix_is_always_blocked(self):
        # Actual SM2 numeric table bytes: q:/ followed by one valid Unicode scalar.
        data = bytes.fromhex('a09c308a724c4aa14c9981bd713a2fe2bea022d884faa098c41c398de5692589')
        release.Payload([]).put('libcrypto.a', data)
        release.Payload([]).put('short-non-path.lib', b'\0q:/\xe2\xbe\xa0\0')
        for text in ('J:/私有/源代码.cpp', '/home/developer/private/build', 'C:\\Users\\Alice\\project'):
            for encoding in ('utf-8', 'utf-16le', 'utf-16be'):
                with self.subTest(text=text, encoding=encoding), self.assertRaises(ValueError):
                    # A non-string byte before a known private prefix cannot hide it.
                    release.Payload([text.encode()]).put('exact-prefix.lib', b'\xbd' + text.encode(encoding) + b'\x81')
        for text in ('J:/私有/源代码.cpp', 'C:\\Users\\Alice\\project', '/home/developer/private/build'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                release.Payload([]).put('actual-path.lib', b'\0' + text.encode() + b'\0')

    def test_failed_input_preserves_failure_receipt_and_does_not_overwrite(self):
        meta = self.fixture()
        self.put(self.install, 'lib/tansr_sdk.lib', b'!<arch>\nJ:\\tansr\\source.cpp')
        output = self.root / 'failure'
        with self.assertRaises(ValueError):
            release.prepare(self.install, self.deps, output, meta)
        self.assertEqual(json.loads((output / 'preparation-receipt.private.json').read_bytes())['status'], 'failed')
        with self.assertRaises(ValueError):
            release.prepare(self.install, self.deps, output, meta)

    def test_selected_tree_unexpected_file_and_wrong_executable_fail(self):
        meta = self.fixture()
        self.put(self.install, 'include/tansr/not-a-header.env', 'private')
        with self.assertRaises(ValueError):
            release.prepare(self.install, self.deps, self.root / 'unexpected', meta)
        for system in ('windows', 'linux', 'macos'):
            with self.subTest(system=system), self.assertRaises(ValueError):
                release.inspect_executable(b'not-native', system)


if __name__ == '__main__':
    unittest.main()
