// Exercise the production pack loader/ownership without hooks or a game process.
#include "../src/core/button_prompts.cpp"
namespace eth {
uintptr_t GameAddr(uintptr_t a) { return a; }
bool SafeReadBytes(uintptr_t a, void* out, size_t n) { memcpy(out, (void*)a, n); return true; }
std::string Sha256FileHex(const std::string&) { return {}; }
void LogMsg(LogLevel, const char*, ...) {}
}
namespace eth::game { void* Device() { return nullptr; } }

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char** argv)
{
    using namespace eth::prompts;
    CHECK(argc == 2);
    const auto texture = std::filesystem::canonical(argv[1]);
    const std::string root = texture.parent_path().string();
    const std::string relative = texture.filename().string();
    const std::vector<Region> xbox{{1,0,0,32,32,34}, {15,0,0,32,32,34}};
    std::string error;
    CHECK(!Load("a", root, relative, xbox, error)); // Hooks unavailable.
    installed = true; // Fixture boundary only; never install hooks in this test.
    CHECK(Load("a", root, relative, xbox, error));
    CHECK(owner == "a" && pack.regions.size() == 2 && pack.width == 256);
    CHECK(!Load("b", root, relative, xbox, error));
    Release("b");
    CHECK(owner == "a");
    CHECK(!Load("a", root, texture.string(), xbox, error)); // Absolute path.
    CHECK(!Load("a", root, "..\\trainer\\mod.lua", xbox, error));
    CHECK(error.find("outside") != std::string::npos);
    CHECK(!Load("a", root, "missing.tga", xbox, error));
    CHECK(!Load("a", root, "mod.lua", xbox, error)); // Existing non-texture file.
    CHECK(!Load("a", root, relative + ":stream", xbox, error));
    CHECK(!Load("a", root, std::string("buttons.tga\0hidden", 18), xbox, error));
    CHECK(!Load("a", root, relative, {{31,0,0,32,32,34}}, error));
    CHECK(!Load("a", root, relative, {{1,250,0,32,32,34}}, error));
    CHECK(owner == "a" && pack.regions.size() == 2); // Atomic failure.
    atlases.emplace(nullptr, std::make_unique<Atlas>());
    CHECK(Load("a", root, relative, {{30,32,0,16,24,19}}, error));
    CHECK(atlases.empty() && pack.regions[0].code == 30 && pack.regions[0].advance == 19);
    Release("a");
    CHECK(owner.empty() && pack.pixels.empty() && atlases.empty());
    CHECK(Load("b", root, relative, {{2,64,0,24,16,27}}, error));
    Release("b");
    installed = false;
    Shutdown();
    std::puts("Button prompt production loader/ownership/replacement checks passed");
}
