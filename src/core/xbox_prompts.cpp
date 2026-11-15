#include "xbox_prompts.h"
#include "xbox_prompt_pixels.h"
#include "game.h"
#include "util.h"
#include "log.h"
#include <MinHook.h>

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
const uint8_t* icons = nullptr;
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
    if (!icons || !font || !font->texture || !font->glyphs ||
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
    unsigned columns = w / 34, rows = (17 + columns - 1) / columns;
    unsigned newH = 1;
    while (newH < h + rows * 34) newH <<= 1;
    if (newH > 4096) return {};
    std::vector<uint32_t> combined(size_t(w) * newH, 0);
    // Keep original artwork at the bottom: native font UVs have an inverted V.
    std::copy(pixels.begin(), pixels.end(), combined.begin() + size_t(newH - h) * w);
    for (auto& glyph : atlas->glyphs) {
        glyph.v0 *= float(h) / newH;
        glyph.v1 *= float(h) / newH;
        const int icon = IconForCode(glyph.code);
        if (icon < 0) continue;
        const unsigned x = (icon % columns) * 34 + 1;
        const unsigned y = (icon / columns) * 34 + 1;
        // Native text samples V bottom-to-top (the original font TGA glyphs
        // are inverted too); flip each upright user image on upload.
        for (unsigned row = 0; row < 32; ++row)
            memcpy(combined.data() + size_t(y + row) * w + x,
                   icons + icon * 4096 + (31 - row) * 128, 128);
        glyph.advance = 34;
        glyph.height = 32;
        glyph.kern = 0;
        glyph.u0 = float(x) / w; glyph.u1 = float(x + 32) / w;
        glyph.v0 = 1.0f - float(y + 32) / newH;
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
    ETH_INFO("Xbox prompts: font %p atlas %ux%u (%u glyphs)", font, w, newH, font->count);
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
            if (!it->second) ETH_WARN("Xbox prompts: unsupported/unavailable font %p; retaining native text", f);
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
    auto resource = FindResourceA(g_selfModule, MAKEINTRESOURCEA(310), RT_RCDATA);
    auto data = resource ? LoadResource(g_selfModule, resource) : nullptr;
    auto bytes = data ? static_cast<const uint8_t*>(LockResource(data)) : nullptr;
    const uint32_t header[] = {0x31504258, 32, 32, 22};
    if (!bytes || SizeofResource(g_selfModule, resource) != 16 + 22 * 4096 ||
        memcmp(bytes, header, 16)) return false;
    icons = bytes + 16;
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
            ETH_WARN("Xbox prompts: hook fingerprint mismatch at %08x", unsigned(targets[i]));
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
    ETH_INFO("Xbox prompt font hooks installed");
    return true;
}

bool SetEnabled(const std::string& mod, bool enabled)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!enabled) { if (owner == mod) owner.clear(); return true; }
    if (!installed || (!owner.empty() && owner != mod)) return false;
    owner = mod;
    return true;
}
void Release(const std::string& mod) { SetEnabled(mod, false); }
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
}
}
