// ===========================================================================
//  Lua bindings: game / memory / input / hook / settings / log / sdk
// ===========================================================================
#include "lua_host.h"
#include "game.h"
#include "input.h"
#include "util.h"
#include "log.h"
#include "mods.h"
#include "overlay.h"
#include "button_prompts.h"
#include "mission_timer.h"
#include "trainer_values.h"
#include "trainer_equipment.h"
#include "trainer_movement.h"
#include "cosmetic_editor.h"
#include "cosmetic_blood.h"
#include "cosmetic_glow.h"
#include "ethhhpatch_runtime.h"
#include "cosmetic_model_host.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace eth::lua {

namespace {

const char* ModId(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "eth.modid");
    const char* id = lua_tostring(L, -1);
    lua_pop(L, 1);
    return id ? id : "";
}

int VkFromLua(lua_State* L, int idx)
{
    if (lua_isnumber(L, idx))
        return (int)lua_tointeger(L, idx);
    const char* s = luaL_checkstring(L, idx);
    std::string k = s;
    for (auto& c : k) c = (char)toupper((unsigned char)c);
    if (k.size() == 1) {
        if (k[0] >= 'A' && k[0] <= 'Z') return k[0];
        if (k[0] >= '0' && k[0] <= '9') return k[0];
    }
    if (k == "SPACE") return VK_SPACE;
    if (k == "SHIFT") return VK_SHIFT;
    if (k == "CTRL" || k == "CONTROL") return VK_CONTROL;
    if (k == "ALT") return VK_MENU;
    if (k == "TAB") return VK_TAB;
    if (k == "ENTER" || k == "RETURN") return VK_RETURN;
    if (k == "ESC" || k == "ESCAPE") return VK_ESCAPE;
    if (k == "UP") return VK_UP;
    if (k == "DOWN") return VK_DOWN;
    if (k == "LEFT") return VK_LEFT;
    if (k == "RIGHT") return VK_RIGHT;
    if (k == "LBUTTON") return VK_LBUTTON;
    if (k == "RBUTTON") return VK_RBUTTON;
    if (k == "MBUTTON") return VK_MBUTTON;
    if (k.size() >= 2 && k[0] == 'F') {
        int n = atoi(k.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    return 0;
}

int PushStratTable(lua_State* L, const game::StratView& s, float camDist = -1.0f)
{
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)s.addr); lua_setfield(L, -2, "addr");
    lua_pushnumber(L, s.pos[0]); lua_setfield(L, -2, "x");
    lua_pushnumber(L, s.pos[1]); lua_setfield(L, -2, "y");
    lua_pushnumber(L, s.pos[2]); lua_setfield(L, -2, "z");
    lua_pushnumber(L, s.pos[3]); lua_setfield(L, -2, "w");
    lua_pushnumber(L, s.quat[0]); lua_setfield(L, -2, "qx");
    lua_pushnumber(L, s.quat[1]); lua_setfield(L, -2, "qy");
    lua_pushnumber(L, s.quat[2]); lua_setfield(L, -2, "qz");
    lua_pushnumber(L, s.quat[3]); lua_setfield(L, -2, "qw");
    lua_pushinteger(L, (lua_Integer)s.flags); lua_setfield(L, -2, "flags");
    lua_pushinteger(L, (lua_Integer)s.typeMask); lua_setfield(L, -2, "type_mask");
    lua_pushinteger(L, (lua_Integer)(uintptr_t)s.pClass); lua_setfield(L, -2, "class");
    lua_pushinteger(L, (lua_Integer)s.next); lua_setfield(L, -2, "next");
    if (s.className[0]) { lua_pushstring(L, s.className); lua_setfield(L, -2, "class_name"); }
    lua_pushinteger(L, s.selfHandle); lua_setfield(L, -2, "self_handle");
    lua_pushinteger(L, (int)game::ClassRole(s.className)); lua_setfield(L, -2, "role");
    if (camDist >= 0.0f) { lua_pushnumber(L, camDist); lua_setfield(L, -2, "camera_distance"); }
    return 1;
}

// ------------------------------------------------------------------ game ----
int g_tick(lua_State* L)        { lua_pushinteger(L, game::TickCount()); return 1; }
int g_cosmetic_materials(lua_State* L)
{
    const auto view = cosmetic::Inspect();
    lua_newtable(L);
    int i = 0;
    for (const auto& material : view.materials) {
        lua_newtable(L);
        lua_pushlstring(L, material.name.data(), material.name.size()); lua_setfield(L, -2, "name");
        lua_pushinteger(L, material.rgb); lua_setfield(L, -2, "rgb");
        lua_pushlstring(L,material.part.data(),material.part.size()); lua_setfield(L,-2,"part");
        lua_rawseti(L, -2, ++i);
    }
    lua_pushlstring(L, view.status.data(), view.status.size());
    lua_pushboolean(L, view.active);
    lua_newtable(L);
    lua_pushboolean(L,view.waiting); lua_setfield(L,-2,"waiting");
    lua_pushinteger(L,view.heads); lua_setfield(L,-2,"heads");
    lua_pushboolean(L,view.textures); lua_setfield(L,-2,"textures");
    lua_pushinteger(L,lua_Integer(view.texturePasses)); lua_setfield(L,-2,"texture_passes");
    lua_pushinteger(L,lua_Integer(view.textureRefusals)); lua_setfield(L,-2,"texture_refusals");
    const auto textureStats=cosmetic::textures::GetPerformanceCounters();
    lua_pushinteger(L,lua_Integer(textureStats.uploads)); lua_setfield(L,-2,"texture_uploads");
    lua_pushinteger(L,lua_Integer(view.fullInspections)); lua_setfield(L,-2,"full_inspections");
    lua_pushinteger(L,lua_Integer(view.fastTicks)); lua_setfield(L,-2,"fast_ticks");
    lua_pushnumber(L,view.lastTickUs/1000.0); lua_setfield(L,-2,"tick_ms");
    lua_pushnumber(L,view.maxTickUs/1000.0); lua_setfield(L,-2,"peak_tick_ms");
    lua_pushnumber(L,view.lastFullTickUs/1000.0); lua_setfield(L,-2,"inspection_ms");
    lua_pushlstring(L,view.textureStatus.data(),view.textureStatus.size()); lua_setfield(L,-2,"texture_status");
    lua_pushlstring(L,view.sashStatus.data(),view.sashStatus.size()); lua_setfield(L,-2,"sash_status");
    return 4;
}
int g_set_cosmetic_textures(lua_State* L)
{
    std::string error;
    std::vector<std::string> paths;
    if (!lua_istable(L,1) || lua_rawlen(L,1)!=3) error="Expected blue, secondary-body and head relative TGA paths.";
    for (int i=1;i<=3 && error.empty();++i) {
        lua_rawgeti(L,1,i);
        size_t length=0;
        const char* path=lua_type(L,-1)==LUA_TSTRING?lua_tolstring(L,-1,&length):nullptr;
        if (!path || !length || length>512) error="Invalid relative TGA path.";
        else paths.emplace_back(path,length);
        lua_pop(L,1);
    }
    const auto* mod=mods::ModManager::Get().Find(ModId(L));
    if (!mod && error.empty()) error="Mod identity unavailable.";
    const bool ok=error.empty() && cosmetic::LoadTextures(mod->id,mod->dir,paths,error,lua_toboolean(L,2)!=0);
    lua_pushboolean(L,ok);
    lua_pushlstring(L,error.data(),error.size());
    return 2;
}
int g_clear_cosmetic_textures(lua_State* L)
{
    cosmetic::ClearTextures(ModId(L)); return 0;
}
int g_set_cosmetic_blood(lua_State* L)
{
    std::string error;
    std::vector<std::string> paths;
    if (!lua_istable(L,1) || lua_rawlen(L,1)!=5) error="Expected five relative blood TGA paths.";
    for (int i=1;i<=5 && error.empty();++i) {
        lua_rawgeti(L,1,i);
        size_t length=0;
        const char* path=lua_type(L,-1)==LUA_TSTRING?lua_tolstring(L,-1,&length):nullptr;
        if (!path || !length || length>512) error="Invalid relative blood TGA path.";
        else paths.emplace_back(path,length);
        lua_pop(L,1);
    }
    int isInteger=0;
    const auto rgb=lua_tointegerx(L,2,&isInteger);
    if (!isInteger || rgb<0 || rgb>0xFFFFFF) error="Expected packed RGB color.";
    const auto* mod=mods::ModManager::Get().Find(ModId(L));
    if (!mod && error.empty()) error="Mod identity unavailable.";
    if (error.empty() && !patch::runtime::InstallCosmeticBloodHook())
        error="Blood binder verification/hook installation refused.";
    const auto native=uint32_t(rgb);
    const auto textureRgb=((native&255)<<16)|(native&0xFF00)|((native>>16)&255);
    const bool wantGlow=lua_toboolean(L,3)!=0;
    const bool glowReady=error.empty() && wantGlow && patch::runtime::InstallCosmeticGlowHooks();
    const bool ok=error.empty() && cosmetic::blood::Configure(mod->id,mod->dir,paths,textureRgb,error,glowReady);
    if (ok && wantGlow && !glowReady) error="Blood/interior colors armed; glow hook verification refused. Glow stays native.";
    lua_pushboolean(L,ok); lua_pushlstring(L,error.data(),error.size());
    return 2;
}
int g_clear_cosmetic_blood(lua_State* L)
{
    cosmetic::blood::Release(ModId(L)); return 0;
}
int g_set_cosmetic_glow_enabled(lua_State* L)
{
    const bool enabled=lua_toboolean(L,1)!=0;
    std::string error;
    if (enabled && !patch::runtime::InstallCosmeticGlowHooks()) error="Glow hook verification refused.";
    const bool ok=error.empty() && cosmetic::blood::SetGlowEnabled(ModId(L),enabled);
    if (!ok && error.empty()) error="Apply blood color first; this choice will apply with the next color request.";
    lua_pushboolean(L,ok); lua_pushlstring(L,error.data(),error.size()); return 2;
}
int g_cosmetic_blood_status(lua_State* L)
{
    const auto stats=cosmetic::blood::GetPerformanceCounters();
    const auto glow=cosmetic::blood::glow::GetStats();
    const auto text=std::string("Blood draw substitutions: ")+std::to_string(stats.substitutions)+
        " (splatter "+std::to_string(stats.kinds[0]+stats.kinds[1]+stats.kinds[2]+stats.kinds[3])+
        ", pools "+std::to_string(stats.kinds[4]+stats.kinds[5]+stats.kinds[6])+")\nSource SHA: "+
        std::to_string(stats.hashes)+"; split interiors: "+std::to_string(stats.kinds[7])+
        "; byte-verdict reuse: "+std::to_string(stats.byteCacheHits)+
        "; uploads: "+std::to_string(stats.uploads)+
        "; deferred: "+std::to_string(stats.deferred)+"; cache full: "+std::to_string(stats.saturated)+
        "\nBlood SDK tick work (last / peak, includes cold uploads): "+
        std::to_string(stats.lastTickUs/1000.0)+" / "+std::to_string(stats.maxTickUs/1000.0)+" ms\nShared glow: "+
        (glow.faulted?"stopped after a state-restore failure":glow.enabled?"armed":"native")+
        "; draws "+std::to_string(glow.draws)+"; refused "+std::to_string(glow.refused)+
        "; uploads "+std::to_string(glow.uploads)+"; color op/args "+
        std::to_string(glow.colorOp)+"/"+std::to_string(glow.arg1)+"/"+std::to_string(glow.arg2);
    lua_pushlstring(L,text.data(),text.size()); return 1;
}
int g_cosmetic_models(lua_State* L)
{
    const auto view = cosmetic::model_host::Inspect(lua_toboolean(L, 1) != 0);
    lua_newtable(L);
    int i = 0;
    for (const auto& candidate : view.candidates) {
        lua_newtable(L);
        lua_pushinteger(L,candidate.addr); lua_setfield(L,-2,"addr");
        lua_pushlstring(L,candidate.name.data(),candidate.name.size()); lua_setfield(L,-2,"name");
        lua_rawseti(L,-2,++i);
    }
    lua_pushlstring(L,view.status.data(),view.status.size());
    lua_newtable(L);
    lua_pushinteger(L,view.captured); lua_setfield(L,-2,"captured");
    lua_pushinteger(L,view.substituted); lua_setfield(L,-2,"substituted");
    lua_pushinteger(L,view.waiting); lua_setfield(L,-2,"waiting");
    lua_pushinteger(L,view.refused); lua_setfield(L,-2,"refused");
    return 3;
}
int g_set_cosmetic_model(lua_State* L)
{
    const char* channel = luaL_checkstring(L,1);
    const auto recipient = luaL_checkinteger(L,2), donor = luaL_checkinteger(L,3);
    const bool sword = !strcmp(channel,"sword");
    lua_pushboolean(L, (!strcmp(channel,"player") || sword) &&
        recipient >= 0 && recipient <= UINT32_MAX && donor > 0 && donor <= UINT32_MAX &&
        cosmetic::model_host::RequestDonor(ModId(L),sword,uintptr_t(recipient),uintptr_t(donor)));
    return 1;
}
int g_set_cosmetic_sword(lua_State* L)
{
    const auto sword = luaL_checkinteger(L,1), tier = luaL_checkinteger(L,2);
    lua_pushboolean(L, sword > 0 && sword <= UINT32_MAX && tier >= 0 && tier <= 5 &&
        cosmetic::model_host::RequestSword(ModId(L),uintptr_t(sword),int(tier)));
    return 1;
}
int g_clear_cosmetic_models(lua_State* L)
{
    cosmetic::model_host::Release(ModId(L)); return 0;
}
int g_set_cosmetic_colors(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    const size_t count = lua_rawlen(L, 1);
    if (count > 128) { lua_pushboolean(L, false); return 1; }
    std::vector<cosmetic::Material> colors;
    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, 1, int(i));
        luaL_checktype(L, -1, LUA_TTABLE);
        lua_getfield(L, -1, "name");
        size_t length = 0;
        const char* name = luaL_checklstring(L, -1, &length);
        const std::string bounded(name, length);
        lua_pop(L, 1);
        lua_getfield(L, -1, "rgb");
        const auto rgb = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L,-1,"part");
        size_t partLength = 0;
        const char* part = luaL_optlstring(L,-1,"body",&partLength);
        const std::string scope(part,partLength);
        lua_pop(L, 2);
        if (rgb < 0 || rgb > 0xFFFFFF || bounded.find('\0') != std::string::npos) {
            lua_pushboolean(L, false); return 1;
        }
        colors.push_back({bounded, uint32_t(rgb),scope});
    }
    lua_pushboolean(L, cosmetic::Set(ModId(L), colors));
    return 1;
}
int g_clear_cosmetic_colors(lua_State* L)
{
    cosmetic::Release(ModId(L));
    return 0;
}
int g_frame(lua_State* L)       { lua_pushinteger(L, game::FrameCount()); return 1; }
int g_time(lua_State* L)        { lua_pushnumber(L, game::GameTime()); return 1; }
int g_time_step(lua_State* L)   { lua_pushnumber(L, game::TimeStep()); return 1; }
int g_timescale(lua_State* L)   { lua_pushnumber(L, game::TimeScale()); return 1; }
int g_set_timescale(lua_State* L) { game::SetTimeScale((float)luaL_checknumber(L, 1)); return 0; }
int g_stop_mission_timer(lua_State* L) {
    lua_pushboolean(L, mission_timer::SetStopped(ModId(L), lua_toboolean(L, 1) != 0));
    return 1;
}

int g_debug_flags(lua_State* L) { lua_pushinteger(L, game::DebugFlags()); return 1; }
int g_trainer_supported(lua_State* L) { lua_pushboolean(L, trainer::Supported()); return 1; }
int g_trainer_value(lua_State* L)
{
    double value = 0;
    uintptr_t address = 0;
    if (!trainer::Read(luaL_checkstring(L, 1), value, address)) {
        lua_pushnil(L); return 1;
    }
    lua_pushnumber(L, value);
    lua_pushinteger(L, address);
    return 2;
}
int g_set_trainer_value(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    const double value = luaL_checknumber(L, 2);
    const auto address = luaL_checkinteger(L, 3);
    const bool equipmentField = !strcmp(name, "sword") || !strcmp(name, "belt") || !strcmp(name, "ranks");
    lua_pushboolean(L, !equipmentField && address > 0 && address <= UINT32_MAX &&
        trainer::Write(name, value, uintptr_t(address)));
    return 1;
}
int g_set_trainer_equipment(lua_State* L)
{
    const char* field = luaL_checkstring(L, 1);
    const auto value = luaL_checkinteger(L, 2);
    const auto address = luaL_checkinteger(L, 3);
    lua_pushboolean(L, value >= 0 && value <= 63 && address > 0 && address <= UINT32_MAX &&
        trainer::equipment::Request(ModId(L), field, int(value), uintptr_t(address)));
    return 1;
}
int g_trainer_equipment_status(lua_State* L)
{
    const auto status = trainer::equipment::Status(ModId(L));
    lua_pushlstring(L, status.data(), status.size());
    return 1;
}
int g_cancel_trainer_equipment(lua_State* L)
{
    trainer::equipment::Release(ModId(L));
    return 0;
}
int g_set_trainer_movement(lua_State* L)
{
    const float speed = float(luaL_checknumber(L, 1));
    const float jump = float(luaL_checknumber(L, 2));
    lua_pushboolean(L, mission_timer::Available() &&
        trainer_movement::Set(ModId(L), speed, jump));
    return 1;
}
int g_clear_trainer_movement(lua_State* L)
{
    trainer_movement::Release(ModId(L));
    return 0;
}
int g_set_debug_flags(lua_State* L) { game::SetDebugFlags((uint16_t)luaL_checkinteger(L, 1)); return 0; }
int g_toggle_flag(lua_State* L)
{
    int bit = 0;
    if (lua_isnumber(L, 1)) bit = (int)lua_tointeger(L, 1);
    else {
        const char* name = luaL_checkstring(L, 1);
        std::string n = name;
        for (auto& c : n) c = (char)toupper((unsigned char)c);
        if (n == "LEVEL_SELECT") bit = 0x001;
        else if (n == "DEMO_LEVEL_SELECT") bit = 0x002;
        else if (n == "SOUND") bit = 0x004;
        else if (n == "PAL") bit = 0x008;
        else if (n == "DEBUG_CHEATS") bit = 0x010;
        else if (n == "DEBUG_TEXT") bit = 0x020;
        else if (n == "DEBUG_FLY") bit = 0x040;
        else if (n == "DEMO_BUILD") bit = 0x080;
        else if (n == "DEMO_MAGAZINE") bit = 0x100;
    }
    if (bit) game::ToggleDebugFlag((uint16_t)bit, lua_toboolean(L, 2) != 0);
    return 0;
}

int g_camera(lua_State* L)
{
    game::CameraView c;
    if (!game::ReadCamera(c)) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)c.addr); lua_setfield(L, -2, "addr");
    lua_pushnumber(L, c.x);       lua_setfield(L, -2, "x");
    lua_pushnumber(L, c.y);       lua_setfield(L, -2, "y");
    lua_pushnumber(L, c.z);       lua_setfield(L, -2, "z");
    lua_pushnumber(L, c.fov);     lua_setfield(L, -2, "fov");
    lua_pushnumber(L, c.param104);lua_setfield(L, -2, "param104");
    lua_newtable(L);
    for (int i = 0; i < 16; ++i) { lua_pushnumber(L, c.world[i]); lua_rawseti(L, -2, i + 1); }
    lua_setfield(L, -2, "world");
    lua_newtable(L);
    for (int i = 0; i < 16; ++i) { lua_pushnumber(L, c.view[i]); lua_rawseti(L, -2, i + 1); }
    lua_setfield(L, -2, "view");
    return 1;
}

int g_set_camera_pos(lua_State* L)
{
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float z = (float)luaL_checknumber(L, 3);
    lua_pushboolean(L, game::SetCameraPosition(x, y, z));
    return 1;
}
int g_set_camera_fov(lua_State* L)
{
    lua_pushboolean(L, game::SetCameraFovDegrees((float)luaL_checknumber(L, 1)));
    return 1;
}
int g_camera_fov_deg(lua_State* L)
{
    game::CameraView c;
    if (!game::ReadCamera(c)) { lua_pushnumber(L, 0); return 1; }
    lua_pushnumber(L, c.fov * (180.0f / 3.14159265358979f));
    return 1;
}
int g_update_view(lua_State*) { game::UpdateView(); return 0; }
int g_set_camera_world(lua_State* L)
{
    if (!lua_istable(L, 1)) return 0;
    float m[16] = {};
    for (int i = 0; i < 16; ++i) {
        lua_rawgeti(L, 1, i + 1);
        m[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    lua_pushboolean(L, game::SetCameraWorld(m));
    return 1;
}

int g_strat_count(lua_State* L) { lua_pushinteger(L, (lua_Integer)game::CountStrats()); return 1; }

int g_strat_identity(lua_State* L)
{
    game::StratIdentity s;
    if (!game::ReadStratIdentity((uintptr_t)luaL_checkinteger(L, 1), s)) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushinteger(L, s.addr); lua_setfield(L, -2, "addr");
    lua_pushinteger(L, s.pClass); lua_setfield(L, -2, "class");
    lua_pushinteger(L, s.selfHandle); lua_setfield(L, -2, "self_handle");
    lua_pushinteger(L, s.rawOwnerHandle); lua_setfield(L, -2, "raw_owner_handle");
    lua_pushboolean(L, s.ownerFieldKnown); lua_setfield(L, -2, "owner_field_known");
    lua_pushinteger(L, s.ownerAddr); lua_setfield(L, -2, "owner_addr");
    lua_pushinteger(L, s.model); lua_setfield(L, -2, "model");
    lua_pushinteger(L, s.animation); lua_setfield(L, -2, "animation");
    lua_pushinteger(L, s.nodeMatrices); lua_setfield(L, -2, "node_matrices");
    lua_pushinteger(L, (int)s.role); lua_setfield(L, -2, "role");
    return 1;
}

int g_strats(lua_State* L)
{
    game::CameraView cam;
    bool hasCam = game::ReadCamera(cam);
    int limit = (int)luaL_optinteger(L, 1, 4096);
    auto strats = game::SnapshotStrats((size_t)limit);
    lua_newtable(L);
    int n = 0;
    for (const auto& s : strats) {
        float d = -1.0f;
        if (hasCam) {
            float dx = s.pos[0] - cam.x, dy = s.pos[1] - cam.y, dz = s.pos[2] - cam.z;
            d = sqrtf(dx * dx + dy * dy + dz * dz);
        }
        PushStratTable(L, s, d);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int g_read_strat(lua_State* L)
{
    uintptr_t addr = (uintptr_t)luaL_checkinteger(L, 1);
    game::StratView s;
    if (!game::ReadStrat(addr, s)) { lua_pushnil(L); return 1; }
    return PushStratTable(L, s);
}

int g_write_strat_pos(lua_State* L)
{
    uintptr_t addr = (uintptr_t)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float z = (float)luaL_checknumber(L, 4);
    lua_pushboolean(L, game::WriteStratPos(addr, x, y, z));
    return 1;
}

int g_player(lua_State* L)
{
    auto& pt = game::PlayerTracker::Get();
    game::StratView s;
    if (!pt.Valid() || !game::ReadStrat(pt.Addr(), s)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)s.addr);
    lua_pushnumber(L, s.pos[0]);
    lua_pushnumber(L, s.pos[1]);
    lua_pushnumber(L, s.pos[2]);
    return 4;
}

int g_set_button_prompts(lua_State* L)
{
    if (lua_isnoneornil(L, 1)) {
        prompts::Release(ModId(L));
        lua_pushboolean(L, true); return 1;
    }
    // Validate without raising Lua longjmp errors while owning C++ objects.
    std::string error;
    std::vector<prompts::Region> regions;
    size_t length = 0;
    const char* path = lua_type(L, 1) == LUA_TSTRING ? lua_tolstring(L, 1, &length) : nullptr;
    const size_t count = lua_istable(L, 2) ? lua_rawlen(L, 2) : 0;
    if (!path || !count || count > 30) error = "Expected a relative TGA path and 1..30 region records";
    for (size_t i = 1; error.empty() && i <= count; ++i) {
        lua_rawgeti(L, 2, i);
        prompts::Region region{};
        if (!lua_istable(L, -1)) error = "Each prompt region must be a table";
        else {
            const char* keys[] = {"code", "x", "y", "width", "height", "advance"};
            unsigned* values[] = {&region.code, &region.x, &region.y,
                                  &region.width, &region.height, &region.advance};
            for (unsigned k = 0; k < 6; ++k) {
                lua_getfield(L, -1, keys[k]);
                int integer = 0;
                const auto value = lua_tointegerx(L, -1, &integer);
                if (!integer || value < 0 || value > 4096)
                    error = "Region fields must be integers in 0..4096";
                else *values[k] = unsigned(value);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        regions.push_back(region);
    }
    bool ok = false;
    if (error.empty()) {
        const auto* mod = mods::ModManager::Get().Find(ModId(L));
        if (!mod) error = "Mod identity unavailable";
        else ok = prompts::Load(mod->id, mod->dir, std::string(path, length), regions, error);
    }
    lua_pushboolean(L, ok);
    if (!ok) { lua_pushlstring(L, error.data(), error.size()); return 2; }
    return 1;
}
int g_player_addr(lua_State* L) { lua_pushinteger(L, (lua_Integer)game::PlayerTracker::Get().Addr()); return 1; }
int g_player_detection_info(lua_State* L) { lua_pushstring(L, game::PlayerTracker::Get().DetectionInfo().c_str()); return 1; }
int g_player_mode(lua_State* L)
{
    auto m = game::PlayerTracker::Get().Mode();
    lua_pushstring(L, m == game::PlayerMode::Auto ? "auto" : m == game::PlayerMode::Manual ? "manual" : m == game::PlayerMode::AutoPlus ? "auto+" : "learning");
    return 1;
}
int g_set_player_mode(lua_State* L)
{
    const char* s = luaL_checkstring(L, 1);
    auto& pt = game::PlayerTracker::Get();
    if (!strcmp(s, "auto")) pt.SetMode(game::PlayerMode::Auto);
    else if (!strcmp(s, "auto+") || !strcmp(s, "autoplus")) pt.SetMode(game::PlayerMode::AutoPlus);
    else if (!strcmp(s, "manual")) pt.SetMode(game::PlayerMode::Manual);
    else if (!strcmp(s, "learning")) pt.BeginLearning();
    return 0;
}
int g_select_player(lua_State* L) { game::PlayerTracker::Get().Select((uintptr_t)luaL_checkinteger(L, 1)); return 0; }
int g_learn_player(lua_State*)   { game::PlayerTracker::Get().BeginLearning(); return 0; }
int g_player_candidates(lua_State* L)
{
    int limit = (int)luaL_optinteger(L, 1, 32);
    auto& cands = game::PlayerTracker::Get().Candidates();
    lua_newtable(L);
    int n = 0;
    for (const auto& c : cands) {
        if (n >= limit) break;
        PushStratTable(L, c.strat, c.distanceToCamera);
        lua_pushboolean(L, c.selected);
        lua_setfield(L, -2, "selected");
        lua_pushnumber(L, c.learnScore); lua_setfield(L, -2, "learn_score");
        lua_pushnumber(L, c.activeSpeed); lua_setfield(L, -2, "active_speed");
        lua_pushnumber(L, c.idleSpeed); lua_setfield(L, -2, "idle_speed");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int g_stat_int(lua_State* L)
{
    int v = 0;
    if (game::GetStatInt((int)luaL_checkinteger(L, 1), v)) lua_pushinteger(L, v);
    else lua_pushnil(L);
    return 1;
}
int g_set_stat_int(lua_State* L)
{
    lua_pushboolean(L, game::SetStatInt((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2)));
    return 1;
}
int g_stat_bit(lua_State* L)
{
    bool v = false;
    if (game::GetStatBit((int)luaL_checkinteger(L, 1), v)) lua_pushboolean(L, v);
    else lua_pushnil(L);
    return 1;
}
int g_set_stat_bit(lua_State* L)
{
    lua_pushboolean(L, game::SetStatBit((int)luaL_checkinteger(L, 1), lua_toboolean(L, 2) != 0));
    return 1;
}
int g_native_addr(lua_State* L) { lua_pushinteger(L, (lua_Integer)game::NativeAddr((int)luaL_checkinteger(L, 1))); return 1; }
int g_native_count(lua_State* L) { lua_pushinteger(L, game::off::NativeCount); return 1; }
int g_hwnd(lua_State* L)  { lua_pushinteger(L, (lua_Integer)(uintptr_t)game::GameHwnd()); return 1; }
int g_device(lua_State* L){ lua_pushinteger(L, (lua_Integer)(uintptr_t)game::Device()); return 1; }
int g_renderer(lua_State* L){ lua_pushinteger(L, (lua_Integer)(uintptr_t)game::Renderer()); return 1; }

// ---------------------------------------------------------------- memory ----
int m_read_byte(lua_State* L)
{
    uint8_t v = 0;
    if (SafeRead((uintptr_t)luaL_checkinteger(L, 1), v)) lua_pushinteger(L, v); else lua_pushnil(L);
    return 1;
}
int m_read_int(lua_State* L)
{
    int32_t v = 0;
    if (SafeRead((uintptr_t)luaL_checkinteger(L, 1), v)) lua_pushinteger(L, v); else lua_pushnil(L);
    return 1;
}
int m_read_float(lua_State* L)
{
    float v = 0;
    if (SafeRead((uintptr_t)luaL_checkinteger(L, 1), v)) lua_pushnumber(L, v); else lua_pushnil(L);
    return 1;
}
int m_read_bytes(lua_State* L)
{
    uintptr_t addr = (uintptr_t)luaL_checkinteger(L, 1);
    int size = (int)luaL_checkinteger(L, 2);
    if (size <= 0 || size > 4096 || !IsReadable((void*)addr, (size_t)size)) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char*)addr, (size_t)size);
    return 1;
}
int m_read_string(lua_State* L)
{
    uintptr_t addr = (uintptr_t)luaL_checkinteger(L, 1);
    int maxLen = (int)luaL_optinteger(L, 2, 256);
    if (maxLen <= 0 || maxLen > 4096 || !IsReadable((void*)addr, 1)) { lua_pushnil(L); return 1; }
    const char* p = (const char*)addr;
    int len = 0;
    while (len < maxLen && p[len]) ++len;
    lua_pushlstring(L, p, (size_t)len);
    return 1;
}
int m_write_byte(lua_State* L)
{
    uint8_t v = (uint8_t)luaL_checkinteger(L, 2);
    lua_pushboolean(L, SafeWrite((uintptr_t)luaL_checkinteger(L, 1), v));
    return 1;
}
int m_write_int(lua_State* L)
{
    int32_t v = (int32_t)luaL_checkinteger(L, 2);
    lua_pushboolean(L, SafeWrite((uintptr_t)luaL_checkinteger(L, 1), v));
    return 1;
}
int m_write_float(lua_State* L)
{
    float v = (float)luaL_checknumber(L, 2);
    lua_pushboolean(L, SafeWrite((uintptr_t)luaL_checkinteger(L, 1), v));
    return 1;
}
int m_write_bytes(lua_State* L)
{
    uintptr_t addr = (uintptr_t)luaL_checkinteger(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    lua_pushboolean(L, SafeWriteBytes(addr, data, len));
    return 1;
}
int m_is_readable(lua_State* L)
{
    uintptr_t addr = (uintptr_t)luaL_checkinteger(L, 1);
    size_t size = (size_t)luaL_optinteger(L, 2, 1);
    lua_pushboolean(L, IsReadable((void*)addr, size));
    return 1;
}
int m_base(lua_State* L) { lua_pushinteger(L, (lua_Integer)ModuleBase()); return 1; }
int m_addr(lua_State* L) { lua_pushinteger(L, (lua_Integer)GameAddr((uintptr_t)luaL_checkinteger(L, 1))); return 1; }

// ----------------------------------------------------------------- input ----
int i_key(lua_State* L)         { lua_pushboolean(L, input::KeyDown(VkFromLua(L, 1))); return 1; }
int i_lease_movement(lua_State* L) { lua_pushboolean(L, input::LeaseMovementKeys((uintptr_t)luaL_checkinteger(L, 1), lua_toboolean(L, 2) != 0)); return 1; }
int i_key_pressed(lua_State* L) { lua_pushboolean(L, input::KeyPressed(VkFromLua(L, 1))); return 1; }
int i_mouse_down(lua_State* L)  { lua_pushboolean(L, input::MouseDown((int)luaL_optinteger(L, 1, 0))); return 1; }
int i_mouse_delta(lua_State* L)
{
    float dx = 0, dy = 0;
    input::GetMouseDelta(dx, dy);
    lua_pushnumber(L, dx);
    lua_pushnumber(L, dy);
    return 2;
}
int i_consume_mouse(lua_State*) { input::ConsumeMouseDelta(); return 0; }
int i_set_capture(lua_State* L) { lua_pushboolean(L, input::SetCapture(lua_toboolean(L, 1) != 0)); return 1; }
int i_capture(lua_State* L)     { lua_pushboolean(L, input::CaptureActive()); return 1; }
int i_overlay_open(lua_State* L){ lua_pushboolean(L, overlay::IsVisible()); return 1; }
int i_key_name(lua_State* L)    { lua_pushstring(L, input::KeyName(VkFromLua(L, 1))); return 1; }

// ------------------------------------------------------------------ hook ----
int h_add(lua_State* L)
{
    const char* ev = luaL_checkstring(L, 1);
    if (!lua_isfunction(L, 2)) return luaL_error(L, "hook.add(event, function)");
    if (strcmp(ev, "tick") && strcmp(ev, "gui") && strcmp(ev, "overlay_open") && strcmp(ev, "overlay_close"))
        return luaL_error(L, "unknown hook event '%s'", ev);

    lua_getfield(L, LUA_REGISTRYINDEX, "eth.hooks");
    lua_getfield(L, -1, ev);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, ev);
    }
    int idx = (int)lua_rawlen(L, -1) + 1;
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, idx);
    lua_pop(L, 2);
    return 0;
}

// -------------------------------------------------------------- settings ----
int s_get(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    const char* def = luaL_optstring(L, 2, "");
    lua_pushstring(L, mods::ModManager::Get().GetSetting(ModId(L), key, def).c_str());
    return 1;
}
int s_set(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    const char* val = luaL_checkstring(L, 2);
    mods::ModManager::Get().SetSetting(ModId(L), key, val);
    return 0;
}
int s_get_float(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    float def = (float)luaL_optnumber(L, 2, 0.0);
    lua_pushnumber(L, mods::ModManager::Get().GetSettingFloat(ModId(L), key, def));
    return 1;
}
int s_set_float(lua_State* L)
{
    mods::ModManager::Get().SetSettingFloat(ModId(L), luaL_checkstring(L, 1), (float)luaL_checknumber(L, 2));
    return 0;
}
int s_get_bool(lua_State* L)
{
    const char* key = luaL_checkstring(L, 1);
    bool def = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, mods::ModManager::Get().GetSettingBool(ModId(L), key, def));
    return 1;
}
int s_set_bool(lua_State* L)
{
    mods::ModManager::Get().SetSettingBool(ModId(L), luaL_checkstring(L, 1), lua_toboolean(L, 2) != 0);
    return 0;
}

// ------------------------------------------------------------------- log ----
int BuildLogText(lua_State* L, char* out, size_t outSize)
{
    int n = lua_gettop(L);
    out[0] = 0;
    size_t used = 0;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (s && used + len + 2 < outSize) {
            if (used) { out[used++] = ' '; }
            memcpy(out + used, s, len);
            used += len;
            out[used] = 0;
        }
        lua_pop(L, 1);
    }
    return 0;
}
int lg_info(lua_State* L)  { char b[2048]; BuildLogText(L, b, sizeof(b)); ETH_INFO("[lua] %s", b); return 0; }
int lg_warn(lua_State* L)  { char b[2048]; BuildLogText(L, b, sizeof(b)); ETH_WARN("[lua] %s", b); return 0; }
int lg_error(lua_State* L) { char b[2048]; BuildLogText(L, b, sizeof(b)); ETH_ERROR("[lua] %s", b); return 0; }

// ------------------------------------------------------------------- sdk ----
int sd_read_file(lua_State* L)
{
    std::string path = luaL_checkstring(L, 1);
    if (path.size() > 1 && path[1] != ':')
        path = JoinPath(lua::ModId(L) ? JoinPath(GetModsDir(), ModId(L)) : GetModsDir(), path);
    std::string data;
    if (!ReadFileToString(path, data)) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, data.data(), data.size());
    return 1;
}
int sd_write_file(lua_State* L)
{
    std::string path = luaL_checkstring(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    if (path.size() > 1 && path[1] != ':')
        path = JoinPath(JoinPath(GetModsDir(), ModId(L)), path);
    lua_pushboolean(L, WriteStringToFile(path, std::string(data, len)));
    return 1;
}
int sd_open_mods(lua_State*) { mods::ModManager::Get().OpenModsFolder(); return 0; }

void SetFuncs(lua_State* L, const luaL_Reg* funcs)
{
    lua_newtable(L);
    for (const luaL_Reg* f = funcs; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
}

} // namespace

void RegisterApi(lua_State* L, const mods::ModInfo& mod)
{
    static const luaL_Reg gameFuncs[] = {
        { "set_button_prompts", g_set_button_prompts },
        { "tick", g_tick }, { "frame", g_frame }, { "time", g_time },
        { "time_step", g_time_step }, { "timescale", g_timescale }, { "set_timescale", g_set_timescale },
        { "stop_mission_timer", g_stop_mission_timer },
        { "trainer_supported", g_trainer_supported },
        { "cosmetic_materials", g_cosmetic_materials },
        { "set_cosmetic_textures", g_set_cosmetic_textures },
        { "clear_cosmetic_textures", g_clear_cosmetic_textures },
        { "set_cosmetic_blood", g_set_cosmetic_blood },
        { "clear_cosmetic_blood", g_clear_cosmetic_blood },
        { "cosmetic_blood_status", g_cosmetic_blood_status },
        { "set_cosmetic_glow_enabled", g_set_cosmetic_glow_enabled },
        { "cosmetic_models", g_cosmetic_models },
        { "set_cosmetic_model", g_set_cosmetic_model },
        { "set_cosmetic_sword", g_set_cosmetic_sword },
        { "clear_cosmetic_models", g_clear_cosmetic_models },
        { "set_cosmetic_colors", g_set_cosmetic_colors },
        { "clear_cosmetic_colors", g_clear_cosmetic_colors },
        { "trainer_value", g_trainer_value }, { "set_trainer_value", g_set_trainer_value },
        { "set_trainer_equipment", g_set_trainer_equipment },
        { "trainer_equipment_status", g_trainer_equipment_status },
        { "cancel_trainer_equipment", g_cancel_trainer_equipment },
        { "set_trainer_movement", g_set_trainer_movement },
        { "clear_trainer_movement", g_clear_trainer_movement },
        { "debug_flags", g_debug_flags }, { "set_debug_flags", g_set_debug_flags }, { "toggle_flag", g_toggle_flag },
        { "camera", g_camera }, { "set_camera_pos", g_set_camera_pos }, { "set_camera_fov", g_set_camera_fov },
        { "camera_fov_deg", g_camera_fov_deg }, { "update_view", g_update_view }, { "set_camera_world", g_set_camera_world },
        { "strat_count", g_strat_count }, { "strats", g_strats }, { "read_strat", g_read_strat },
        { "write_strat_pos", g_write_strat_pos }, { "strat_identity", g_strat_identity },
        { "player", g_player }, { "player_addr", g_player_addr }, { "player_mode", g_player_mode },
        { "set_player_mode", g_set_player_mode }, { "select_player", g_select_player },
        { "learn_player", g_learn_player }, { "player_candidates", g_player_candidates },
        { "player_detection_info", g_player_detection_info },
        { "stat_int", g_stat_int }, { "set_stat_int", g_set_stat_int },
        { "stat_bit", g_stat_bit }, { "set_stat_bit", g_set_stat_bit },
        { "native_addr", g_native_addr }, { "native_count", g_native_count },
        { "hwnd", g_hwnd }, { "device", g_device }, { "renderer", g_renderer },
        { nullptr, nullptr }
    };
    SetFuncs(L, gameFuncs);
    lua_pushinteger(L, 0x001); lua_setfield(L, -2, "FLAG_LEVEL_SELECT");
    lua_pushinteger(L, 0x002); lua_setfield(L, -2, "FLAG_DEMO_LEVEL_SELECT");
    lua_pushinteger(L, 0x004); lua_setfield(L, -2, "FLAG_SOUND");
    lua_pushinteger(L, 0x008); lua_setfield(L, -2, "FLAG_PAL");
    lua_pushinteger(L, 0x010); lua_setfield(L, -2, "FLAG_DEBUG_CHEATS");
    lua_pushinteger(L, 0x020); lua_setfield(L, -2, "FLAG_DEBUG_TEXT");
    lua_pushinteger(L, 0x040); lua_setfield(L, -2, "FLAG_DEBUG_FLY");
    lua_pushinteger(L, 0x080); lua_setfield(L, -2, "FLAG_DEMO_BUILD");
    lua_pushinteger(L, 0x100); lua_setfield(L, -2, "FLAG_DEMO_MAGAZINE");
    lua_setglobal(L, "game");

    static const luaL_Reg memFuncs[] = {
        { "read_byte", m_read_byte }, { "read_int", m_read_int }, { "read_float", m_read_float },
        { "read_bytes", m_read_bytes }, { "read_string", m_read_string },
        { "write_byte", m_write_byte }, { "write_int", m_write_int }, { "write_float", m_write_float },
        { "write_bytes", m_write_bytes },
        { "is_readable", m_is_readable }, { "base", m_base }, { "addr", m_addr },
        { nullptr, nullptr }
    };
    SetFuncs(L, memFuncs);
    lua_setglobal(L, "memory");

    static const luaL_Reg inputFuncs[] = {
        { "key", i_key }, { "key_pressed", i_key_pressed }, { "mouse_down", i_mouse_down },
        { "lease_movement_keys", i_lease_movement },
        { "mouse_delta", i_mouse_delta }, { "consume_mouse", i_consume_mouse },
        { "set_capture", i_set_capture }, { "capture", i_capture },
        { "overlay_open", i_overlay_open }, { "key_name", i_key_name },
        { nullptr, nullptr }
    };
    SetFuncs(L, inputFuncs);
    lua_setglobal(L, "input");

    static const luaL_Reg hookFuncs[] = {
        { "add", h_add },
        { nullptr, nullptr }
    };
    SetFuncs(L, hookFuncs);
    lua_setglobal(L, "hook");

    static const luaL_Reg settingFuncs[] = {
        { "get", s_get }, { "set", s_set },
        { "get_float", s_get_float }, { "set_float", s_set_float },
        { "get_bool", s_get_bool }, { "set_bool", s_set_bool },
        { nullptr, nullptr }
    };
    SetFuncs(L, settingFuncs);
    lua_setglobal(L, "settings");

    static const luaL_Reg logFuncs[] = {
        { "info", lg_info }, { "warn", lg_warn }, { "error", lg_error },
        { nullptr, nullptr }
    };
    SetFuncs(L, logFuncs);
    lua_setglobal(L, "log");

    static const luaL_Reg sdkFuncs[] = {
        { "read_file", sd_read_file }, { "write_file", sd_write_file }, { "open_mods_folder", sd_open_mods },
        { nullptr, nullptr }
    };
    SetFuncs(L, sdkFuncs);
    lua_pushstring(L, kVersion); lua_setfield(L, -2, "version");
    lua_pushstring(L, kName);    lua_setfield(L, -2, "name");
    lua_pushstring(L, GetGameDir().c_str()); lua_setfield(L, -2, "game_dir");
    lua_pushstring(L, GetModsDir().c_str()); lua_setfield(L, -2, "mods_dir");
    lua_pushstring(L, mod.dir.c_str());      lua_setfield(L, -2, "mod_dir");
    lua_setglobal(L, "sdk");
}

} // namespace eth::lua
