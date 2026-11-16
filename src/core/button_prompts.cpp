#include "button_prompts.h"
#include "game.h"
#include "util.h"
#include "log.h"
#include <MinHook.h>
#include <filesystem>
#include <fstream>

namespace eth::prompts {
namespace {
struct Glyph {
    int advance, height;
    float u0, v0, u1, v1;
    uint16_t code;
    int16_t kern;
};
static_assert(sizeof(Glyph) == 28);
struct Texture {
    uintptr_t vtable;
    unsigned width, height;
    IDirect3DTexture9* handle;
    const uint8_t* source;
    unsigned size, flags, reserved;
};
static_assert(sizeof(Texture) == 32);
struct Font {
    void* queue;
    Glyph* glyphs;
    unsigned count;
    Texture* texture;
    int width16, height16, pass;
    uint32_t rotated;
    float lineHeight;
    Font* next;
    int clip[4];
    void* labels;
    unsigned labelCount;
    void* strings;
    unsigned stringCount;
    int* substitutions;
};
static_assert(offsetof(Font, substitutions) == 0x48);

struct Atlas {
    Texture texture{};
    std::vector<Glyph> glyphs;
    std::array<int, 32> substitutions{};
    ~Atlas() { if (texture.handle) texture.handle->Release(); }
};
std::map<Font*, std::unique_ptr<Atlas>> atlases;
std::recursive_mutex mutex;
std::string owner;
bool installed = false;
struct Pack {
    unsigned width = 0, height = 0;
    std::vector<uint32_t> pixels;
    std::vector<Region> regions;
};
Pack pack;
using InitFn = void(__thiscall*)(Font*, void*);
using MeasureFn = int(__thiscall*)(Font*, const char**, float, int*);
using QueueFn = void(__thiscall*)(Font*, const char*, float, float, float, float,
                                  unsigned, unsigned, unsigned, unsigned);
using RenderFn = void(__cdecl*)(int);
InitFn initOriginal = nullptr;
MeasureFn measureOriginal = nullptr;
QueueFn queueOriginal = nullptr;
RenderFn renderOriginal = nullptr;
constexpr uintptr_t targets[] = {0x40d3a0, 0x40d470, 0x40d630, 0x448670};

std::unique_ptr<Atlas> Build(Font* font)
{
    if (pack.pixels.empty() || !font || !font->texture || !font->glyphs ||
        font->count < 32 || font->count > 1024 || !font->substitutions ||
        (font->rotated & 255)) return {};
    auto* original = font->texture;
    if (!original->source || original->size > 32 * 1024 * 1024) return {};
    std::vector<uint8_t> source(original->size);
    if (!SafeReadBytes(reinterpret_cast<uintptr_t>(original->source), source.data(), source.size()))
        return {};
    std::vector<uint32_t> pixels;
    unsigned w = 0, h = 0;
    if (!DecodeTga(source.data(), source.size(), w, h, pixels) ||
        w != original->width || h != original->height || w < 64) return {};
    auto atlas = std::make_unique<Atlas>();
    atlas->glyphs.assign(font->glyphs, font->glyphs + font->count);
    std::copy_n(font->substitutions, 32, atlas->substitutions.begin());
    // Pack arbitrary mod regions into padded shelves above the native artwork.
    std::map<unsigned, std::pair<unsigned, unsigned>> positions;
    unsigned px = 1, py = 1, shelf = 0;
    for (const auto& region : pack.regions) {
        if (region.width + 2 > w) return {};
        if (px + region.width + 1 > w) { px = 1; py += shelf + 2; shelf = 0; }
        positions[region.code] = {px, py};
        px += region.width + 2;
        shelf = (std::max)(shelf, region.height);
    }
    unsigned newH = 1;
    while (newH < h + py + shelf + 1) newH <<= 1;
    if (newH > 4096) return {};
    std::vector<uint32_t> combined(size_t(w) * newH, 0);
    // Keep original artwork at the bottom: native font UVs have an inverted V.
    std::copy(pixels.begin(), pixels.end(), combined.begin() + size_t(newH - h) * w);
    for (auto& glyph : atlas->glyphs) {
        glyph.v0 *= float(h) / newH;
        glyph.v1 *= float(h) / newH;
        auto region = std::find_if(pack.regions.begin(), pack.regions.end(),
            [&glyph](const Region& r) { return r.code == glyph.code; });
        if (region == pack.regions.end()) continue;
        const unsigned x = positions[glyph.code].first;
        const unsigned y = positions[glyph.code].second;
        // Native text samples V bottom-to-top (the original font TGA glyphs
        // are inverted too); flip each upright user image on upload.
        for (unsigned row = 0; row < region->height; ++row)
            memcpy(combined.data() + size_t(y + row) * w + x,
                   pack.pixels.data() + size_t(region->y + region->height - 1 - row) *
                       pack.width + region->x, region->width * 4);
        glyph.advance = region->advance;
        glyph.height = region->height;
        glyph.kern = 0;
        glyph.u0 = float(x) / w; glyph.u1 = float(x + region->width) / w;
        glyph.v0 = 1.0f - float(y + region->height) / newH;
        glyph.v1 = 1.0f - float(y) / newH;
        atlas->substitutions[glyph.code] = -1;
    }
    auto* device = static_cast<IDirect3DDevice9*>(game::Device());
    if (!device) return {};
    atlas->texture = *original;
    atlas->texture.handle = nullptr;
    atlas->texture.width = w; atlas->texture.height = newH;
    // Managed textures survive the game's Reset; the engine only borrows this
    // cloned descriptor during text calls, and never owns/releases its handle.
    if (FAILED(device->CreateTexture(w, newH, 1, 0, D3DFMT_A8R8G8B8,
                                    D3DPOOL_MANAGED, &atlas->texture.handle, nullptr))) return {};
    D3DLOCKED_RECT lock{};
    if (FAILED(atlas->texture.handle->LockRect(0, &lock, nullptr, 0))) return {};
    for (unsigned row = 0; row < newH; ++row)
        memcpy(static_cast<uint8_t*>(lock.pBits) + row * lock.Pitch,
               combined.data() + size_t(row) * w, w * 4);
    atlas->texture.handle->UnlockRect(0);
    ETH_INFO("Button prompts: font %p atlas %ux%u (%u glyphs)", font, w, newH, font->count);
    return atlas;
}

// Only borrow fields for the native call. Disabling/unloading never leaves
// pointers in game objects, and normal glyphs retain their original metrics.
struct Scope {
    Font* font = nullptr;
    Glyph* glyphs = nullptr;
    Texture* texture = nullptr;
    int* substitutions = nullptr;
    int width16 = 0, height16 = 0;
    explicit Scope(Font* f) {
        if (owner.empty() || !f || !game::Device()) return;
        auto it = atlases.find(f);
        if (it == atlases.end()) {
            it = atlases.emplace(f, Build(f)).first;
            if (!it->second) ETH_WARN("Button prompts: unsupported/unavailable font %p; retaining native text", f);
        }
        if (!it->second) return;
        auto& a = *it->second;
        font = f; glyphs = f->glyphs; texture = f->texture;
        substitutions = f->substitutions;
        width16 = f->width16; height16 = f->height16;
        f->glyphs = a.glyphs.data(); f->texture = &a.texture;
        f->substitutions = a.substitutions.data();
        f->width16 = a.texture.width * 16; f->height16 = a.texture.height * 16;
    }
    ~Scope() {
        if (!font) return;
        font->glyphs = glyphs; font->texture = texture;
        font->substitutions = substitutions;
        font->width16 = width16; font->height16 = height16;
    }
};

void __fastcall Init(Font* f, void*, void* descriptor)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    atlases.erase(f); // Reused addresses on level/resource reload are not identities.
    initOriginal(f, descriptor);
}
int __fastcall Measure(Font* f, void*, const char** text, float scale, int* count)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    Scope scope(f);
    return measureOriginal(f, text, scale, count);
}
void __fastcall Queue(Font* f, void*, const char* text, float x, float y,
                       float sx, float sy, unsigned a, unsigned r, unsigned g, unsigned b)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    Scope scope(f);
    queueOriginal(f, text, x, y, sx, sy, a, r, g, b);
}
void __cdecl Render(int pass)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    std::vector<std::unique_ptr<Scope>> scopes;
    auto* font = *reinterpret_cast<Font**>(game::A(0x502d9c));
    // Three native font slots; bound traversal in case another mod corrupts a list.
    for (unsigned n = 0; font && n < 32; ++n, font = font->next)
        scopes.emplace_back(std::make_unique<Scope>(font));
    renderOriginal(pass);
}
}

bool Install()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (installed) return true;
    char exe[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH) ||
        Sha256FileHex(exe) != "49099fef26610e84f94f1aa2dd33aa9e83413ee24ff87fed63a8a98cc4f3daf0")
        return false;
    void* detours[] = {(void*)Init, (void*)Measure, (void*)Queue, (void*)Render};
    void** originals[] = {(void**)&initOriginal, (void**)&measureOriginal,
                          (void**)&queueOriginal, (void**)&renderOriginal};
    // All targets must still have their original entry bytes.
    uint8_t first[][6] = {{0xa1,0,0,0,0,0x53}, {0x81,0xec,0x1c,0x01,0,0},
                          {0x81,0xec,0x18,0x01,0,0}, {0x83,0xec,0x78,0xa1,0,0}};
    const uint32_t fontList = uint32_t(game::A(0x502d9c));
    memcpy(first[0] + 1, &fontList, 4);
    for (unsigned i = 0; i < 4; ++i) {
        uint8_t actual[6]{};
        const size_t size = i == 3 ? 4 : 6;
        if (!SafeReadBytes(game::A(targets[i]), actual, size) || memcmp(actual, first[i], size)) {
            ETH_WARN("Button prompts: hook fingerprint mismatch at %08x", unsigned(targets[i]));
            return false;
        }
    }
    unsigned created = 0;
    for (; created < 4; ++created)
        if (MH_CreateHook((void*)game::A(targets[created]), detours[created],
                          originals[created]) != MH_OK) break;
    bool ok = created == 4;
    if (ok) for (auto target : targets)
        if (MH_EnableHook((void*)game::A(target)) != MH_OK) { ok = false; break; }
    if (!ok) {
        for (unsigned i = 0; i < created; ++i) {
            MH_DisableHook((void*)game::A(targets[i]));
            MH_RemoveHook((void*)game::A(targets[i]));
        }
        return false;
    }
    installed = true;
    ETH_INFO("Button prompt font hooks installed");
    return true;
}

bool Load(const std::string& mod, const std::string& modDirectory,
          const std::string& relativeTexture, const std::vector<Region>& regions,
          std::string& error)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    error.clear();
    if (!installed) { error = "Button prompt hooks unavailable"; return false; }
    if (mod.empty() || (!owner.empty() && owner != mod)) {
        error = "Button prompts already owned by another mod"; return false;
    }
    Pack candidate;
    try {
        namespace fs = std::filesystem;
        fs::path relative(relativeTexture);
        if (relative.empty() || relative.is_absolute() || relative.has_root_path() ||
            relativeTexture.find(':') != std::string::npos ||
            relativeTexture.find('\0') != std::string::npos) {
            error = "Texture path must be relative to the mod directory"; return false;
        }
        const auto root = fs::canonical(modDirectory);
        const auto path = fs::canonical(root / relative);
        auto prefix = root.wstring();
        if (prefix.back() != L'\\') prefix += L'\\';
        const auto full = path.wstring();
        if (full.size() <= prefix.size() ||
            _wcsnicmp(full.c_str(), prefix.c_str(), prefix.size()) != 0) {
            error = "Texture resolves outside the mod directory"; return false;
        }
        const auto size = fs::file_size(path);
        if (!size || size > 18 * 1024 * 1024) {
            error = "Texture file exceeds the 18 MiB limit or is empty"; return false;
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        std::ifstream file(path, std::ios::binary);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) ||
            !DecodeTga(bytes.data(), bytes.size(), candidate.width, candidate.height, candidate.pixels)) {
            error = "Invalid or unsupported uncompressed TGA"; return false;
        }
        if (!ValidateRegions(candidate.width, candidate.height, regions)) {
            error = "Invalid prompt regions, dimensions, advances or duplicate control codes"; return false;
        }
        candidate.regions = regions;
    } catch (const std::exception& e) {
        error = std::string("Cannot load prompt texture: ") + e.what(); return false;
    }
    // Failed loads preserve the active pack; successful replacement drops every
    // old font atlas so metrics, substitutions and artwork change together.
    atlases.clear();
    pack = std::move(candidate);
    owner = mod;
    return true;
}
void Release(const std::string& mod)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (owner != mod) return;
    owner.clear();
    atlases.clear();
    pack = {};
}
void Shutdown()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    owner.clear();
    if (installed) for (auto target : targets) {
        MH_DisableHook((void*)game::A(target));
        MH_RemoveHook((void*)game::A(target));
    }
    installed = false;
    atlases.clear();
    pack = {};
}
}
