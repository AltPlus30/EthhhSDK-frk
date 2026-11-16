namespace {
using GreetingClock=std::chrono::steady_clock;
GreetingClock::time_point greetingStart{};
bool greetingStarted=false,greetingSound=false;
#if defined(ETH_TESTING)
float greetingTestSeconds=-1;
#endif
void RenderNotification(bool welcome) {
    auto& cfg=Config::Get();
    if (!welcome && !cfg.GetBool("core","watermark",true)) return;
    const auto& io=ImGui::GetIO();
    if (io.DisplaySize.x<16 || io.DisplaySize.y<16) return;
    const int corner=std::clamp(cfg.GetInt("core","watermark_pos",0),0,3);
    const float margin=12*ui::Scale();
    const ImVec2 pivot(corner%2 ? 1.0f : 0.0f,corner>=2 ? 1.0f : 0.0f);
    ImGui::SetNextWindowPos(ImVec2(pivot.x ? io.DisplaySize.x-margin : margin,
        pivot.y ? io.DisplaySize.y-margin : margin),ImGuiCond_Always,pivot);
    ImGui::SetNextWindowBgAlpha(.9f);
    ImGui::Begin("##ethhh_watermark",nullptr,ImGuiWindowFlags_NoDecoration|
        ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav);
    if (welcome) {
        ImGui::TextUnformatted("EthhhSDK loaded successfully!");
        ImGui::TextUnformatted("You're all set. Let's play.");
    } else {
        ImGui::TextUnformatted("EthhhSDK / I-Ninja");
        const auto rates=patch::ReadStatus();
        if (rates.sampled) ImGui::Text("Game: %.0f FPS / %.0f Hz",rates.gamePresentsPerSecond,rates.activeTicksPerSecond);
        else ImGui::TextUnformatted("Game FPS: waiting for sample");
        const int detail=std::clamp(cfg.GetInt("core","watermark_detail",1),0,2);
        if (detail>=1) ImGui::Text("%s to open menu",input::KeyName(overlay::ToggleKey()));
        if (detail>=2) {
            game::CameraView cam;
            if (game::ReadCamera(cam)) ImGui::Text("cam %.1f %.1f %.1f",cam.x,cam.y,cam.z);
            ImGui::Text("mods %zu/%zu",mods::ModManager::Get().CountLoaded(),mods::ModManager::Get().CountEnabled());
        }
    }
    ImGui::End();
}
}
void ResetStartupWatermark() { greetingStarted=false; greetingSound=false; }
void DrawWatermark() { RenderNotification(false); }
void DrawStartupWatermark() {
    if (!greetingStarted) { greetingStart=GreetingClock::now(); greetingStarted=true; }
    float seconds=std::chrono::duration<float>(GreetingClock::now()-greetingStart).count();
#if defined(ETH_TESTING)
    if (greetingTestSeconds>=0) seconds=greetingTestSeconds;
#endif
    if (seconds<load::kDelay) return;
    const bool welcome=seconds<load::kDelay+load::kDuration;
    if (welcome && !greetingSound) { greetingSound=true; PlayUiSound(Sound::Welcome); }
    RenderNotification(welcome);
}
#if defined(ETH_TESTING)
void TestSetStartupSeconds(float seconds) { greetingTestSeconds=seconds; }
#endif
