#include "overlay_cursor.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>

static int checks = 0;
static void Check(bool ok, const char* label)
{
    ++checks;
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); }
}
int main()
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280,720);
    io.DeltaTime = 1.0f/60;
    io.IniFilename = io.LogFilename = nullptr;
    io.Fonts->Flags |= ImFontAtlasFlags_NoMouseCursors;
    io.Fonts->Build();
    io.Fonts->SetTexID(1);
    for (bool visible : {false,true}) for (bool focused : {false,true})
        for (float scale : {.8f,1.0f,1.5f,2.0f})
            for (int shape : {int(ImGuiMouseCursor_Arrow),int(ImGuiMouseCursor_TextInput),int(ImGuiMouseCursor_None)}) {
                io.AddMousePosEvent(320,240);
                ImGui::NewFrame();
                ImGui::SetMouseCursor((ImGuiMouseCursor)shape);
                eth::overlay::DrawMenuCursor(visible,focused,scale);
                auto* draw = ImGui::GetForegroundDrawList();
                Check((draw->VtxBuffer.Size>0)==(visible&&focused),"cursor geometry only for focused open menu");
                Check(!io.MouseDrawCursor,"no dependency on built-in software cursor");
                ImGui::Render();
                if (visible&&focused) {
                    const auto* data = ImGui::GetDrawData();
                    Check(data->CmdListsCount>0 && data->TotalVtxCount>0,"cursor reaches final draw data");
                    Check(data->CmdLists[data->CmdListsCount-1]==draw,"cursor is in topmost foreground list");
                }
            }
    for (ImVec2 pos : {ImVec2(-FLT_MAX,-FLT_MAX),ImVec2(-1,20),ImVec2(1280,20),
                      ImVec2(20,720),ImVec2(std::numeric_limits<float>::infinity(),20)}) {
        ImGui::NewFrame();
        io.MousePos = pos;
        eth::overlay::DrawMenuCursor(true,true,1);
        Check(ImGui::GetForegroundDrawList()->VtxBuffer.Size==0,"invalid/outside pointer is not drawn");
        ImGui::Render();
    }
    ImGui::DestroyContext();
    std::printf("PASS geometry overlay cursor: %d assertions\n",checks);
}
