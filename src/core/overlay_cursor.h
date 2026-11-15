#pragma once
#include "imgui.h"
#include <cmath>

namespace eth::overlay {

// Geometry-only cursor: independent of the game's ShowCursor counter, native
// SetCursor calls, font-atlas cursor textures and mod-requested cursor hiding.
inline void DrawMenuCursor(bool visible, bool focused, float scale)
{
    const auto& io = ImGui::GetIO();
    const ImVec2 p = io.MousePos;
    if (!visible || !focused || !std::isfinite(p.x) || !std::isfinite(p.y) ||
        p.x < 0 || p.y < 0 || p.x >= io.DisplaySize.x || p.y >= io.DisplaySize.y)
        return;
    if (!std::isfinite(scale) || scale <= 0) scale = 1;
    auto* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRectFullScreen();
    if (ImGui::GetMouseCursor() == ImGuiMouseCursor_TextInput) {
        const float x = p.x, y0 = p.y - 8 * scale, y1 = p.y + 8 * scale;
        for (int pass = 0; pass < 2; ++pass) {
            const ImU32 color = pass ? IM_COL32_WHITE : IM_COL32_BLACK;
            const float thickness = (pass ? 1.5f : 3.5f) * scale;
            draw->AddLine(ImVec2(x,y0),ImVec2(x,y1),color,thickness);
            draw->AddLine(ImVec2(x-4*scale,y0),ImVec2(x+4*scale,y0),color,thickness);
            draw->AddLine(ImVec2(x-4*scale,y1),ImVec2(x+4*scale,y1),color,thickness);
        }
    } else {
        const ImVec2 points[] = {
            p, ImVec2(p.x,p.y+19*scale), ImVec2(p.x+5*scale,p.y+14*scale),
            ImVec2(p.x+9*scale,p.y+22*scale), ImVec2(p.x+12*scale,p.y+20*scale),
            ImVec2(p.x+8*scale,p.y+12*scale), ImVec2(p.x+15*scale,p.y+12*scale)
        };
        // Explicit triangles keep the concave arrow fill well-defined.
        draw->AddTriangleFilled(points[0],points[1],points[2],IM_COL32_WHITE);
        draw->AddTriangleFilled(points[0],points[2],points[5],IM_COL32_WHITE);
        draw->AddTriangleFilled(points[0],points[5],points[6],IM_COL32_WHITE);
        draw->AddQuadFilled(points[2],points[3],points[4],points[5],IM_COL32_WHITE);
        draw->AddPolyline(points,IM_ARRAYSIZE(points),IM_COL32_BLACK,ImDrawFlags_Closed,1.5f*scale);
    }
    draw->PopClipRect();
}

} // namespace eth::overlay
