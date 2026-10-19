#pragma once

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>

namespace DockStyle {

inline bool Begin(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0) {
    // Docked windows normally paint an opaque rectangular background. Make it
    // transparent so an inset, rounded background can be drawn without changing
    // the docking geometry or splitter hit targets.
    ImGuiWindow* existingWindow = ImGui::FindWindowByName(name);
    ImGuiDockNode* existingRoot = existingWindow ? existingWindow->DockNode : nullptr;
    while (existingRoot && existingRoot->ParentNode)
        existingRoot = existingRoot->ParentNode;
    if (existingRoot && existingRoot->IsDockSpace())
        ImGui::SetNextWindowBgAlpha(0.0f);

    const bool visible = ImGui::Begin(name, open, flags);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGuiDockNode* node = window->DockNode;
    ImGuiDockNode* root = node;
    while (root && root->ParentNode)
        root = root->ParentNode;
    if (!node || !root->IsDockSpace() || (node->VisibleWindow && node->VisibleWindow != window))
        return visible;

    const float gap = 0.0f;
    const float rounding = ImMax(4.0f, ImGui::GetStyle().WindowRounding);
    const ImVec2 min(node->Pos.x + gap, node->Pos.y + gap);
    const ImVec2 max(node->Pos.x + node->Size.x - gap, node->Pos.y + node->Size.y - gap);
    if (max.x <= min.x || max.y <= min.y)
        return visible;

    ImDrawList* drawList = node->HostWindow ? node->HostWindow->DrawList : window->DrawList;
    if (node->HostWindow)
        drawList->ChannelsSetCurrent(DOCKING_HOST_DRAW_CHANNEL_BG);
    // The host draw list spans the entire dockspace. Clip each panel's custom
    // geometry so antialiased edges cannot bleed into a neighboring dock at a
    // T-junction.
    drawList->PushClipRect(node->Pos, max, true);

    // Match the backing to ImGui's dock splitter color so only the pixels cut
    // away by the rounded corners are visible, without creating an extra gap.
    drawList->AddRectFilled(node->Pos, ImVec2(node->Pos.x + node->Size.x, node->Pos.y + node->Size.y),
                            ImGui::GetColorU32(ImGuiCol_Border));
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_WindowBg), rounding);
    drawList->PopClipRect();
    if (node->HostWindow)
        drawList->ChannelsSetCurrent(DOCKING_HOST_DRAW_CHANNEL_FG);
    drawList->PushClipRect(node->Pos, max, true);

    // Dock tab bars are drawn after window backgrounds and would otherwise
    // square off the top corners. Mask only the pixels outside the same radius.
    const ImVec4 border = ImGui::GetStyleColorVec4(ImGuiCol_Border);
    const ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const ImU32 cornerColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
        border.x * border.w + background.x * (1.0f - border.w),
        border.y * border.w + background.y * (1.0f - border.w),
        border.z * border.w + background.z * (1.0f - border.w), 1.0f));
    constexpr int cornerSegments = 6;
    constexpr float halfPi = 1.57079632679f;
    constexpr float overscan = 1.0f;
    const ImVec2 topLeftCenter(min.x + rounding, min.y + rounding);
    drawList->PathLineTo(ImVec2(min.x - overscan, min.y - overscan));
    drawList->PathLineTo(ImVec2(min.x + rounding, min.y - overscan));
    for (int i = 0; i <= cornerSegments; ++i) {
        const float angle = -halfPi - halfPi * static_cast<float>(i) / cornerSegments;
        drawList->PathLineTo(ImVec2(topLeftCenter.x + std::cos(angle) * rounding,
                                    topLeftCenter.y + std::sin(angle) * rounding));
    }
    drawList->PathLineTo(ImVec2(min.x - overscan, min.y + rounding));
    drawList->PathFillConcave(cornerColor);

    const ImVec2 topRightCenter(max.x - rounding, min.y + rounding);
    drawList->PathLineTo(ImVec2(max.x - rounding, min.y - overscan));
    drawList->PathLineTo(ImVec2(max.x + overscan, min.y - overscan));
    drawList->PathLineTo(ImVec2(max.x + overscan, min.y + rounding));
    for (int i = cornerSegments; i >= 0; --i) {
        const float angle = -halfPi + halfPi * static_cast<float>(i) / cornerSegments;
        drawList->PathLineTo(ImVec2(topRightCenter.x + std::cos(angle) * rounding,
                                    topRightCenter.y + std::sin(angle) * rounding));
    }
    drawList->PathFillConcave(cornerColor);
    drawList->PopClipRect();

    return visible;
}

} // namespace DockStyle
