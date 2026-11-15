#include "config.h"
#include "ethhhpatch.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks = 0;
void Check(bool ok, const char* label)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
bool Same(const eth::patch::DisplayPlan& a, const eth::patch::DisplayPlan& b)
{
    return a.width == b.width && a.height == b.height &&
        a.windowed == b.windowed && a.renderHz == b.renderHz;
}
}

int main(int argc, char** argv)
{
    // Explicit workspace-local output; never use a game installation or OS temp.
    Check(argc == 2, "pass a new workspace-local INI fixture path");
    const std::string path = argv[1];
    Check(GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES, "fixture must not overwrite a file");
    auto& cfg = eth::Config::Get();
    Check(!cfg.Load(path), "new config creates defaults");
    eth::patch::DisplayPlan saved;
    Check(!eth::patch::LoadStartupPlan(saved), "default startup opt-out");
    for (bool windowed : {true, false}) {
        eth::patch::DisplayPlan plan{2560, 1440, 0, windowed};
        Check(eth::patch::SavePlan(plan), "save editable display");
        Check(cfg.Load(path), "reload config from disk");
        Check(Same(eth::patch::LoadPlan(), plan), "resolution and window mode round trip");
        Check(!eth::patch::LoadStartupPlan(saved), "saving edits alone never arms startup");
    }
    eth::patch::DisplayPlan applied{1920, 1080, 0, true};
    Check(eth::patch::SaveStartupPlan(applied), "remember verified mode");
    Check(cfg.Load(path), "reload startup config");
    Check(eth::patch::LoadStartupPlan(saved) && Same(saved, applied), "windowed startup survives restart");
    Check(eth::patch::RememberDisplay(), "legacy saved startup migrates to remember on");
    eth::patch::DisplayPlan future{3840, 2160, 60, false};
    Check(eth::patch::SavePlan(future), "save different future plan");
    Check(cfg.Load(path), "reload separate plans");
    Check(Same(eth::patch::LoadPlan(), future), "future plan retained");
    Check(eth::patch::LoadStartupPlan(saved) && Same(saved, applied), "future edits cannot replace verified startup");
    Check(eth::patch::DisableStartupPlan(), "disarm for restore or recovery");
    Check(cfg.Load(path) && !eth::patch::LoadStartupPlan(saved), "disabled startup survives restart");
    Check(eth::patch::RememberDisplay(), "crash disarming preserves remember preference");
    Check(Same(eth::patch::LoadPlan(), future), "forget startup retains editable plan");
    Check(eth::patch::SaveStartupPlan(future), "remember fullscreen mode");
    Check(cfg.Load(path) && eth::patch::LoadStartupPlan(saved) && Same(saved, future), "fullscreen startup survives restart");
    auto invalid = future;
    invalid.width = 0;
    Check(!eth::patch::SavePlan(invalid), "invalid edits refused");
    Check(!eth::patch::SaveStartupPlan(invalid), "invalid startup refused");
    Check(cfg.Load(path) && Same(eth::patch::LoadPlan(), future), "invalid edits preserve last valid settings");
    cfg.SetInt("ethhhpatch_startup", "height", 0);
    Check(!eth::patch::LoadStartupPlan(saved), "corrupt startup not silently replaced by defaults");
    Check(eth::patch::SetRememberDisplay(false), "turn remember off");
    Check(cfg.Load(path) && !eth::patch::RememberDisplay() && !eth::patch::LoadStartupPlan(saved),
        "remember off and disabled startup survive disk reload");
    Check(eth::patch::SetRememberDisplay(true), "turn remember on");
    Check(cfg.Load(path) && eth::patch::RememberDisplay() && !eth::patch::LoadStartupPlan(saved),
        "enabling preference alone cannot arm an invalid or unverified mode");
    Check(eth::patch::SaveStartupPlan(applied), "save mode after enabling remember");
    Check(eth::patch::SetRememberDisplay(false), "disable a valid remembered mode");
    Check(cfg.Load(path) && !eth::patch::LoadStartupPlan(saved), "off clears a previously valid startup mode");
    Check(DeleteFileA(path.c_str()) != FALSE, "remove fixture");
    std::printf("PASS production display config persistence: %d assertions\n", checks);
    return 0;
}
