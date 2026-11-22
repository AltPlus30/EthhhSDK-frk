// Tint/head unit tests use the real editor with a separate isolated texture ABI.
// The production texture module has its own fake-D3D tests.
namespace eth::cosmetic::texture_fixture {
inline bool allowLoad=false, allowPass=false, installOK=true;
inline unsigned installs=0, passes=0;
inline std::vector<textures::Binding> observed;
}
namespace eth::patch::runtime {
bool InstallCosmeticTextureHooks() {
    ++cosmetic::texture_fixture::installs;
    return cosmetic::texture_fixture::installOK;
}
}
namespace eth::cosmetic::textures {
bool Decode(const uint8_t*,size_t,Image&) { return false; }
bool Load(const std::string&,const std::string&,const std::vector<std::string>&,std::string&) {
    return texture_fixture::allowLoad;
}
void Release(const std::string&) {}
void Shutdown() {
    texture_fixture::allowLoad=texture_fixture::allowPass=false;
    texture_fixture::installOK=true; texture_fixture::installs=texture_fixture::passes=0;
    texture_fixture::observed.clear();
}
void InvalidatePrepared() { texture_fixture::observed.clear(); }
void SuspendPrepared() {}
bool PrepareBatch(const std::vector<Binding>& bindings) {
    texture_fixture::observed=bindings;
    return !bindings.empty();
}
bool DrawPrepared(DrawContinuation draw,void* context) {
    if (!texture_fixture::allowPass || texture_fixture::observed.empty()) return false;
    ++texture_fixture::passes; draw(context); return true;
}
bool BatchDraw(const std::vector<Binding>& bindings,DrawContinuation draw,void* context) {
    texture_fixture::observed=bindings;
    if (!texture_fixture::allowPass || bindings.empty()) return false;
    ++texture_fixture::passes;
    draw(context);
    return true;
}
}
namespace eth::cosmetic::sash {
Result ApplyKnown(const game::StratIdentity&,uint32_t,const std::vector<game::StratIdentity>&) { return Result::Waiting; }
void RestoreKnown(const game::StratIdentity&,const std::vector<game::StratIdentity>&) {}
void Invalidate() {}
}
