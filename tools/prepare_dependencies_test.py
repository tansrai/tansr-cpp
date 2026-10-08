"""离线验证依赖准备的归档边界，临时根只使用系统 temp。"""
import io
from pathlib import Path
import tarfile
import tempfile
import unittest

from prepare_dependencies import extract


class Extraction(unittest.TestCase):
    def archive(self, directory, entries):
        path = directory / 'source.tar'
        with tarfile.open(path, 'w') as stream:
            for name, kind, value in entries:
                item = tarfile.TarInfo(name)
                item.type = kind
                if kind == tarfile.REGTYPE:
                    data = value.encode()
                    item.size = len(data)
                    stream.addfile(item, io.BytesIO(data))
                else:
                    item.linkname = value
                    stream.addfile(item)
        return path

    def reject(self, entries):
        with tempfile.TemporaryDirectory(prefix='tansr-cpp-extract-') as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                extract(self.archive(root, entries), root / 'result')
            self.assertFalse((root / 'result').exists())

    def test_parent_traversal(self):
        self.reject([('../escape', tarfile.REGTYPE, 'x')])

    def test_symlink_escape(self):
        self.reject([('root/link', tarfile.SYMTYPE, '../../escape')])

    def test_hardlink_escape(self):
        self.reject([('root/link', tarfile.LNKTYPE, '../escape')])

    def test_windows_drive_and_stream(self):
        for name in ('C:/file', 'root/file:stream', 'root\\file'):
            self.reject([(name, tarfile.REGTYPE, 'x')])

    def test_windows_aliases_and_reserved_devices(self):
        for name in ('root/.. /escape', 'root/a.', 'root/NUL', 'root/CON.txt', 'root/./a'):
            self.reject([(name, tarfile.REGTYPE, 'x')])

    def test_link_ancestor(self):
        self.reject([('root/link', tarfile.SYMTYPE, 'target'),
                     ('root/link/file', tarfile.REGTYPE, 'x')])

    def test_link_cycle(self):
        self.reject([('root/a', tarfile.SYMTYPE, 'b'), ('root/b', tarfile.SYMTYPE, 'a')])

    def test_duplicate(self):
        self.reject([('root/file', tarfile.REGTYPE, 'x'), ('root/file', tarfile.REGTYPE, 'y')])

    def test_device(self):
        self.reject([('root/dev', tarfile.CHRTYPE, '')])

    def test_internal_file_links(self):
        with tempfile.TemporaryDirectory(prefix='tansr-cpp-extract-') as directory:
            root = Path(directory)
            result = extract(self.archive(root, [('root/actual', tarfile.REGTYPE, 'trusted'),
                ('root/sub/alias', tarfile.SYMTYPE, '../actual'),
                ('root/hard', tarfile.LNKTYPE, 'root/actual')]), root / 'result')
            for name in ('actual', 'sub/alias', 'hard'):
                self.assertEqual((result / name).read_text(), 'trusted')
                self.assertFalse((result / name).is_symlink())


if __name__ == '__main__':
    unittest.main()
