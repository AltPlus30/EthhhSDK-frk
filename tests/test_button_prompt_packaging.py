"""Offline packaging tests; no game install or external artwork required."""
import json
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PackagingTests(unittest.TestCase):
    def stage(self, source, output):
        return subprocess.run([
            "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
            str(ROOT / "tools/stage-lua-mods.ps1"), "-ModsDir", str(source),
            "-DistDir", str(output),
        ], capture_output=True, text=True, errors="replace")

    def test_user_supplied_pack_and_nested_asset(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temp:
            temp = Path(temp)
            mod = temp / "source" / "CustomButtons"
            (mod / "textures").mkdir(parents=True)
            (mod / "mod.lua").write_text("-- user-supplied script", encoding="utf-8")
            (mod / "textures/custom.tga").write_bytes(b"user-supplied-texture")
            manifest = {"type": "lua", "name": "Custom", "entry": "mod.lua",
                        "assets": ["textures/custom.tga"]}
            (mod / "mod.json").write_text(json.dumps(manifest), encoding="utf-8")
            result = self.stage(temp / "source", temp / "dist")
            self.assertEqual(result.returncode, 0, result.stderr)
            staged = temp / "dist/CustomButtons"
            self.assertEqual((staged / "textures/custom.tga").read_bytes(), b"user-supplied-texture")
            self.assertEqual((staged / "mod.lua").read_bytes(), (mod / "mod.lua").read_bytes())
            self.assertEqual(json.loads((staged / "mod.json").read_text()), manifest)

    def test_invalid_assets_refused(self):
        for asset in ("../outside.tga", "missing.tga", "mod.json", "C:\\outside.tga",
                      "image.tga:stream", 123):
            with self.subTest(asset=asset), tempfile.TemporaryDirectory(dir=ROOT / "build") as temp:
                temp = Path(temp)
                mod = temp / "source/Pack"
                mod.mkdir(parents=True)
                (temp / "source/outside.tga").write_bytes(b"outside")
                (mod / "mod.lua").write_text("-- fixture")
                (mod / "mod.json").write_text(json.dumps({
                    "type": "lua", "entry": "mod.lua", "assets": [asset]}))
                self.assertNotEqual(self.stage(temp / "source", temp / "dist").returncode, 0)

    def test_sdk_has_no_embedded_platform_art(self):
        self.assertFalse((ROOT / "src/core/xbox_prompts.bgra").exists())
        self.assertNotIn("xbox_prompts.rc", (ROOT / "CMakeLists.txt").read_text())
        source = (ROOT / "src/core/button_prompts.cpp").read_text()
        self.assertNotIn("FindResource", source)
        self.assertNotIn("Xbox", source)
        self.assertNotIn("IconForCode", source)

    def test_built_sdk_has_no_old_xbox_resource(self):
        # DATAFILE never executes the DLL or DllMain.
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
        kernel.LoadLibraryExW.restype = ctypes.c_void_p
        kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        kernel.FindResourceW.restype = ctypes.c_void_p
        kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
        module = kernel.LoadLibraryExW(str(ROOT / "build/dist/EthhhSDK.dll"), None, 2)
        self.assertTrue(module, ctypes.get_last_error())
        try:
            self.assertFalse(kernel.FindResourceW(module, 310, 10))
        finally:
            kernel.FreeLibrary(module)


if __name__ == "__main__":
    unittest.main()
