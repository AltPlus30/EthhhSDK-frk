#include "imgui_ext.h"
#include "config.h"
#include "ui_audio.h"
#include "imgui_internal.h"
#include <cfloat>

namespace eth::ui {
namespace {
ImU32 g_accent = IM_COL32(0, 122, 255, 255);
float g_scale = 1, g_dpi = 1, g_managerOpacity = 1;
ImFont *g_regular = nullptr, *g_small = nullptr, *g_bold = nullptr, *g_title = nullptr;
}
float Scale() { return g_scale; }
float Dpi() { return g_dpi; }
ImU32 Accent() { return g_accent; }
ImU32 WithAlpha(ImU32 col, float alpha) {
    const auto a = ImU32(((col >> IM_COL32_A_SHIFT) & 255) * std::clamp(alpha, 0.0f, 1.0f));
    return (col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}
ImU32 AccentDim(float alpha) { return WithAlpha(g_accent, alpha); }
ImU32 ThemeInk() { return ImGui::GetColorU32(ImGuiCol_Text); }
ImU32 ThemeSoftInk() { return ImGui::GetColorU32(ImGuiCol_TextDisabled); }
ImU32 ThemeSurface() { return ImGui::GetColorU32(ImGuiCol_WindowBg); }
ImFont* FontRegular() { return g_regular; }
ImFont* FontSmall() { return g_small; }
ImFont* FontBold() { return g_bold; }
ImFont* FontTitle() { return g_title; }
ImU32 HexToU32(const char* hex, ImU32 fallback) {
    if (!hex) return fallback;
    while (*hex == '#') ++hex;
    if (strlen(hex) < 6) return fallback;
    unsigned r=0,g=0,b=0,a=255;
    if (sscanf_s(hex,"%02x%02x%02x",&r,&g,&b)!=3) return fallback;
    if (strlen(hex)>=8) sscanf_s(hex+6,"%02x",&a);
    return IM_COL32(r,g,b,a);
}
ImVec4 HexToColor(const char* hex) { return ImGui::ColorConvertU32ToFloat4(HexToU32(hex)); }
void U32ToHex(ImU32 col, char out[7]) {
    sprintf_s(out,7,"%02X%02X%02X",(col>>IM_COL32_R_SHIFT)&255,
        (col>>IM_COL32_G_SHIFT)&255,(col>>IM_COL32_B_SHIFT)&255);
}
void SetAccent(ImU32 color) { g_accent=color; ApplyTheme(); }
void EnsureContext() {
    if (ImGui::GetCurrentContext()) return;
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename=nullptr; io.LogFilename=nullptr;
    ApplyTheme();
}
void ApplyTheme() {
    auto& style=ImGui::GetStyle();
    style=ImGuiStyle();
    if (Config::Get().GetInt("theme","appearance",0)==1) ImGui::StyleColorsLight();
    else ImGui::StyleColorsDark();
    for (auto col : {ImGuiCol_CheckMark,ImGuiCol_SliderGrab,ImGuiCol_NavCursor})
        style.Colors[col]=ImGui::ColorConvertU32ToFloat4(g_accent);
    style.ScaleAllSizes(g_scale);
}
void LoadFonts(float scale) {
    EnsureContext();
    g_dpi=scale; g_scale=std::clamp(scale,.75f,2.5f);
    auto& io=ImGui::GetIO(); io.Fonts->Clear();
    auto font=[&](float px) {
        ImFontConfig cfg; cfg.SizePixels=px*g_scale;
        return io.Fonts->AddFontDefault(&cfg);
    };
    g_regular=font(15); g_small=font(12); g_bold=font(15); g_title=font(28);
    io.FontDefault=g_regular; io.Fonts->Build(); ApplyTheme(); InitUiAudio();
}
void SetManagerOpacity(float alpha) { g_managerOpacity=std::clamp(alpha,0.0f,1.0f); }
bool ToggleSwitch(const char* id,bool* value,float) { return ImGui::Checkbox(id,value); }
bool Checkbox(const char* label,bool* value) { return ImGui::Checkbox(label,value); }
bool CheckboxFlags(const char* label,unsigned int* value,unsigned int flags) { return ImGui::CheckboxFlags(label,value,flags); }
bool Slider(const char* label,float* value,float low,float high,const char* format) {
    if (!value || !std::isfinite(low) || !std::isfinite(high) || low>high) {
        ImGui::TextDisabled("%s: invalid range",label); return false;
    }
    const float old=*value;
    *value=std::isnan(*value) ? low : std::clamp(*value,low,high);
    // ImGui's float slider requires bounds within half of FLT_MAX.
    const bool changed=ImGui::SliderFloat(label,value,std::max(low,-FLT_MAX/2),
        std::min(high,FLT_MAX/2),format);
    return changed || old!=*value;
}
bool SearchField(const char* id,char* value,size_t capacity,const char* hint) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    return ImGui::InputTextWithHint(id,hint,value,capacity);
}
void SectionHeader(const char* title,const char* subtitle) {
    ImGui::SeparatorText(title);
    if (subtitle && *subtitle) ImGui::TextWrapped("%s",subtitle);
}
void Divider(float spacing) {
    ImGui::Dummy(ImVec2(0,spacing*g_scale)); ImGui::Separator();
    ImGui::Dummy(ImVec2(0,spacing*g_scale));
}
void Badge(const char* text,ImU32 color) {
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color),"%s",text);
}
void HelpMarker(const char* text) {
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize()*28);
        ImGui::TextUnformatted(text); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
    }
}
bool AccentButton(const char* label,const ImVec2& size) { return ImGui::Button(label,size); }
bool GhostButton(const char* label,const ImVec2& size) { return ImGui::Button(label,size); }
bool NavItem(const char* id,const char* label,bool selected,const char*) {
    ImGui::PushID(id); bool clicked=ImGui::Selectable(label,selected);
    ImGui::PopID(); return clicked;
}
void StatRow(const char* label,const char* value) {
    ImGui::TextDisabled("%s",label); ImGui::SameLine(); ImGui::TextUnformatted(value);
}
void DrawCardBg(ImVec2 pos,ImVec2 size,ImU32 color,float rounding) {
    ImGui::GetWindowDrawList()->AddRectFilled(pos,ImVec2(pos.x+size.x,pos.y+size.y),color,rounding);
}
bool BeginCard(const char* id,const ImVec2& size) { return ImGui::BeginChild(id,size,ImGuiChildFlags_Borders); }
void EndCard() { ImGui::EndChild(); }
void DrawLogo(ImVec2 pos,float size) {
    auto* dl=ImGui::GetWindowDrawList();
    for (int i=0;i<3;++i) {
        const float y=pos.y+size*(.28f+i*.20f);
        dl->AddRectFilled(ImVec2(pos.x+size*.27f,y),ImVec2(pos.x+size*(i==1 ? .59f : .71f),y+size*.07f),g_accent);
    }
}
bool BeginChromeWindow(const char* id,const char* title,const char* subtitle,bool* open,
                       const ImVec2& defaultSize,ImGuiWindowFlags flags) {
    EnsureContext();
    if (!(ImGui::GetCurrentContext()->NextWindowData.Flags & ImGuiNextWindowDataFlags_HasSize))
        ImGui::SetNextWindowSize(defaultSize,ImGuiCond_FirstUseEver);
    const std::string name=std::string(title)+"###"+id;
    if (!ImGui::Begin(name.c_str(),open,flags)) {
        ImGui::End(); return false;
    }
    if (subtitle && *subtitle) ImGui::TextDisabled("%s",subtitle);
    return true;
}
void EndChromeWindow() {
    // Include page-owned draw-list primitives as well as standard widgets.
    ImGuiWindow* root=ImGui::GetCurrentWindow();
    if (g_managerOpacity<1) for (auto* window:ImGui::GetCurrentContext()->Windows) {
        auto* parent=window;
        while (parent && parent!=root) parent=parent->ParentWindow;
        if (parent && window->Active)
            for (auto& vertex:window->DrawList->VtxBuffer) vertex.col=WithAlpha(vertex.col,g_managerOpacity);
    }
    ImGui::End();
}
bool BeginTabBarC(const char* id) { return ImGui::BeginTabBar(id); }
bool TabItemC(const char* label,bool selected) { return ImGui::BeginTabItem(label,nullptr,selected ? ImGuiTabItemFlags_SetSelected : 0); }
bool InputTextString(const char* label,std::string& value,size_t maxLen) {
    char buf[1024]; strncpy_s(buf,sizeof(buf),value.c_str(),_TRUNCATE);
    const size_t capacity=std::min(sizeof(buf),maxLen+1); buf[capacity-1]=0;
    if (!ImGui::InputText(label,buf,capacity)) return false;
    value=buf; return true;
}
bool InputTextMultilineString(const char* label,std::string& value,const ImVec2& size) {
    char buf[8192]; strncpy_s(buf,sizeof(buf),value.c_str(),_TRUNCATE);
    if (!ImGui::InputTextMultiline(label,buf,sizeof(buf),size)) return false;
    value=buf; return true;
}
bool ComboString(const char* label,int* current,const std::vector<std::string>& items) {
    if (items.empty()) return false;
    *current=std::clamp(*current,0,int(items.size())-1);
    bool changed=false;
    if (ImGui::BeginCombo(label,items[*current].c_str())) {
        for (int i=0;i<int(items.size());++i)
            if (ImGui::Selectable(items[i].c_str(),*current==i)) { *current=i; changed=true; }
        ImGui::EndCombo();
    }
    return changed;
}
} // namespace eth::ui
