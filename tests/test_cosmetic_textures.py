"""Standard-library tests; no installed game or output writes required."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "extract_ninja_textures", ROOT / "tools/extract-ninja-cosmetic-textures.py")
extractor = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extractor)


class CosmeticTextureTests(unittest.TestCase):
    def fixture(self):
        tga = struct.pack("<BBBHHBHHHHBB",
                          0, 1, 1, 0, 16, 24, 0, 0, 32, 32, 8, 0)
        tga += bytes(16 * 3 + 32 * 32)
        pool = bytearray(struct.pack("<8I", 0x54BC30, 32, 32, 0,
                                     32, len(tga), 1, 0) + tga)
        entry = (0, "fixture", 32, 32, 16, hashlib.sha256(tga).hexdigest())
        return pool, entry, tga

    def test_original_bytes_preserved(self):
        pool, entry, tga = self.fixture()
        name, data = extractor.original_tga(pool, entry)
        self.assertEqual(name, "fixture-A00000000.tga")
        self.assertEqual(data, tga)

    def test_descriptor_corruption_rejected(self):
        pool, entry, _ = self.fixture()
        for offset, value in ((0, 0), (4, 64), (16, 36), (24, 0)):
            with self.subTest(offset=offset):
                altered = bytearray(pool)
                struct.pack_into("<I", altered, offset, value)
                with self.assertRaises(ValueError):
                    extractor.original_tga(altered, entry)

    def test_palette_index_rejected(self):
        pool, entry, _ = self.fixture()
        pool[-1] = 16
        with self.assertRaisesRegex(ValueError, "palette index"):
            extractor.original_tga(pool, entry)

    def test_changed_original_rejected(self):
        pool, entry, _ = self.fixture()
        pool[32 + 18] = 1
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            extractor.original_tga(pool, entry)

    def test_truncated_payload_rejected(self):
        pool, entry, _ = self.fixture()
        with self.assertRaisesRegex(ValueError, "outside pool"):
            extractor.original_tga(pool[:-1], entry)

    def test_existing_directory_refused_before_game_read(self):
        with self.assertRaisesRegex(ValueError, "Output already exists"):
            extractor.extract(ROOT / "not-an-installed-game", ROOT)

    def test_game_tree_output_refused_before_game_read(self):
        game = ROOT / "not-an-installed-game"
        with self.assertRaisesRegex(ValueError, "outside the installed game"):
            extractor.extract(game, game / "extracted-textures")

    def test_bad_attribution_rejected(self):
        with self.assertRaises(ValueError):
            extractor.validate_attribution(bytes(0x4B3D38))


if __name__ == "__main__":
    unittest.main()
