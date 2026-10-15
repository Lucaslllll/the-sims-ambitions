import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from tools.ipa_to_obb import APP_PREFIX, ARM64, ARMV7, EXECUTABLE, check_executable, package


def executable(cryptid=0):
    header = struct.pack("<8I", 0xFEEDFACF, ARM64, 0, 2, 1, 24, 0, 0)
    return header + struct.pack("<6I", 0x2C, 24, 0, 0, cryptid, 0)


def executable32(cryptid=0):
    return struct.pack("<7I", 0xFEEDFACE, ARMV7, 9, 2, 1, 20, 0) + struct.pack("<5I", 0x21, 20, 0, 0, cryptid)


class PackageTests(unittest.TestCase):
    def test_armv7_thin_validation_and_fairplay(self):
        self.assertEqual(check_executable(executable32()), {"armv7"})
        with self.assertRaisesRegex(ValueError, "FairPlay"):
            check_executable(executable32(1))

    def test_universal_validates_both_slices(self):
        thin32, thin64 = executable32(), executable()
        table = struct.pack(">2I", 0xCAFEBABE, 2)
        table += struct.pack(">5I", ARMV7, 9, 48, len(thin32), 0)
        table += struct.pack(">5I", ARM64, 0, 48 + len(thin32), len(thin64), 0)
        self.assertEqual(check_executable(table + thin32 + thin64), {"armv7", "arm64"})
        with self.assertRaisesRegex(ValueError, "FairPlay"):
            check_executable(table + executable32(1) + thin64)

    def test_conversion_preserves_game_files_and_excludes_ipa_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            ipa, obb = Path(directory) / "game.ipa", Path(directory) / "game.obb"
            expected = {
                EXECUTABLE: executable(),
                APP_PREFIX + "data/test.bin": bytes(range(256)) * 1000,
            }
            with zipfile.ZipFile(ipa, "w") as source:
                for name, data in expected.items():
                    source.writestr(name, data)
                source.writestr("iTunesMetadata.plist", b"not needed")
            with contextlib.redirect_stdout(io.StringIO()):
                package(ipa, obb)
            with zipfile.ZipFile(obb) as result:
                self.assertEqual(set(result.namelist()), set(expected))
                for name, data in expected.items():
                    self.assertEqual(result.read(name), data)
            original = obb.read_bytes()
            with self.assertRaisesRegex(ValueError, "já existe"):
                package(ipa, obb)
            self.assertEqual(obb.read_bytes(), original)

    def test_encrypted_input_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as directory:
            ipa, obb = Path(directory) / "game.ipa", Path(directory) / "game.obb"
            with zipfile.ZipFile(ipa, "w") as source:
                source.writestr(EXECUTABLE, executable(1))
            with self.assertRaisesRegex(ValueError, "FairPlay"):
                package(ipa, obb)
            self.assertFalse(obb.exists())
            self.assertEqual(list(Path(directory).iterdir()), [ipa])

    def test_fat_arm64_and_invalid_slices(self):
        thin = executable()
        fat = struct.pack(">2I5I", 0xCAFEBABE, 1, ARM64, 0, 32, len(thin), 0)
        fat += b"\0" * 4 + thin
        check_executable(fat)
        with self.assertRaisesRegex(ValueError, "incompleta"):
            check_executable(fat[:-1])
        with self.assertRaises(ValueError):
            check_executable(struct.pack("<8I", 0xFEEDFACF, ARM64, 0, 2, 1, 0, 0, 0))
        wrong_cpu = bytearray(thin)
        struct.pack_into("<I", wrong_cpu, 4, 12)
        with self.assertRaises(ValueError):
            check_executable(wrong_cpu)

    def test_unsafe_archive_path_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            ipa, obb = Path(directory) / "game.ipa", Path(directory) / "game.obb"
            with zipfile.ZipFile(ipa, "w") as source:
                source.writestr(EXECUTABLE, executable())
                source.writestr(APP_PREFIX + "../outside", b"bad path")
            with self.assertRaisesRegex(ValueError, "Caminho inválido"):
                package(ipa, obb)
            self.assertFalse(obb.exists())


if __name__ == "__main__":
    unittest.main()
