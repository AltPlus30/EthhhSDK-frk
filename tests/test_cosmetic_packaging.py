"""Offline integration guards; no game, process or GPU access."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CosmeticPackagingTests(unittest.TestCase):
    def test_loadable_off_by_default_lua_manifest(self):
        manifest = json.loads((ROOT / "mods/CosmeticEditor/mod.json").read_text())
        self.assertEqual(manifest["id"], "cosmetic-editor")
        self.assertEqual(manifest["type"], "lua")
        self.assertFalse(manifest["enabled"])
        self.assertTrue((ROOT / "mods/CosmeticEditor" / manifest["entry"]).is_file())
        self.assertEqual(len(manifest["assets"]),8)
        for asset in manifest["assets"]:
            self.assertTrue(asset.startswith(("textures/", "blood/")) and asset.endswith(".tga"))
            self.assertTrue((ROOT / "mods/CosmeticEditor" / asset).is_file())

    def test_lua_uses_public_cosmetic_api_not_profile_or_raw_memory(self):
        source = (ROOT / "mods/CosmeticEditor/mod.lua").read_text()
        self.assertIn('hook.add("gui"', source)
        for forbidden in ("memory.", "set_stat_", "set_trainer_equipment",
                          'hook.add("tick"', "settings."):
            self.assertNotIn(forbidden, source)
        bindings = (ROOT / "src/core/lua_bindings.cpp").read_text()
        for api in ("cosmetic_materials", "set_cosmetic_colors", "clear_cosmetic_colors"):
            self.assertIn(f'"{api}"', bindings)
            self.assertIn("game." + api, source)
        for api in ("set_cosmetic_blood", "clear_cosmetic_blood"):
            self.assertIn(f'"{api}"', bindings)
            self.assertIn("game." + api, source)
        self.assertIn('"set_cosmetic_glow_enabled"', bindings)
        self.assertIn('game.set_cosmetic_glow_enabled(false)', source)
        self.assertNotIn("set_cosmetic_head_mask", source)
        self.assertNotIn('"set_cosmetic_head_mask"', bindings)

    def test_host_releases_every_native_cosmetic_owner(self):
        source = (ROOT / "src/core/lua_host.cpp").read_text()
        for owner in ("m.id", "mod.id", "id", "kv.first"):
            self.assertIn(f"cosmetic::Release({owner});", source)
            self.assertIn(f"cosmetic::blood::Release({owner});", source)
        source = (ROOT / "src/core/hooks.cpp").read_text()
        self.assertIn("cosmetic::Shutdown();", source)
        self.assertIn("cosmetic::blood::BeforeNativeTick();", source)
        self.assertIn("cosmetic::blood::Tick();", source)
        self.assertLess(source.index("cosmetic::BeforeNativeTick();"),
                        source.index("g_tickOrig(self, tickIndex)"))
        self.assertLess(source.index("cosmetic::blood::BeforeNativeTick();"),
                        source.index("g_tickOrig(self, tickIndex)"))
        self.assertLess(source.index("game::PlayerTracker::Get().Update();"),
                        source.index("cosmetic::OnNativeTickCompleted();"))

    def test_tint_implementation_has_no_gameplay_or_code_writes(self):
        source = (ROOT / "src/core/cosmetic_editor.cpp").read_text()
        for forbidden in ("trainer::Write(", "Fn_SetInt", "pProfile", "MH_CreateHook",
                          "SafeWrite(", "VirtualProtect(", "FlushInstructionCache("):
            self.assertNotIn(forbidden, source)
        self.assertIn("SameAppearance(target, current)", source)
        self.assertIn("0xFF000000", source)
        self.assertIn("game::UniqueNinja(snapshot)", source)
        self.assertIn("head.rawOwnerHandle == ninja.selfHandle",source)
        self.assertNotIn("tracker.Select(",source)

    def test_model_integration_uses_shared_scopes_and_flush_fence(self):
        source = (ROOT / "src/core/ethhhpatch_runtime.cpp").read_text()
        wrapper = source[source.index("void __fastcall EntitySubmitDetour("):
                         source.index("void __fastcall CosmeticFlushDetour(")]
        self.assertIn("cosmetic::models::SubmitEntity", wrapper)
        self.assertIn("EntitySubmitBody, g_entitySubmitOrig", wrapper)
        self.assertIn("constexpr uintptr_t kCosmeticFlush = 0x44A340", source)
        self.assertIn("cosmetic::models::BeforeFlush(self)", source)
        self.assertIn("cosmetic::DrawTextures(ContinueCosmeticOpaque",source)
        self.assertIn("cosmetic::DrawTextures(ContinueCosmeticTransparent",source)
        self.assertIn("VerifyPassHookFingerprints()",source)
        self.assertIn("if (cosmeticFrame) cosmetic::models::EndFrame()", source)
        host = (ROOT / "src/core/cosmetic_model_host.cpp").read_text()
        self.assertIn("!patch::runtime::InNativeRender()", host)
        self.assertIn("!cached(next.recipient)", host)
        for forbidden in ("trainer::Write(", "Notify(", "Fn_SetInt"):
            self.assertNotIn(forbidden, host)
        lua_host = (ROOT / "src/core/lua_host.cpp").read_text()
        for owner in ("m.id", "mod.id", "id", "kv.first"):
            self.assertIn(f"cosmetic::model_host::Release({owner});", lua_host)


if __name__ == "__main__":
    unittest.main()
