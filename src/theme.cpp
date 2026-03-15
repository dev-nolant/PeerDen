#include "theme.h"
#include "imgui.h"
#include <cstdio>

namespace tunngle {

namespace {
float g_ui_scale = 1.0f;
}

void SetUIScale(float scale) {
    g_ui_scale = (scale > 0.5f && scale < 4.0f) ? scale : 1.0f;
}

float GetUIScale() {
    return g_ui_scale;
}

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* paths[] = {
        "build/_deps/imgui-src/misc/fonts/Roboto-Medium.ttf",
        "_deps/imgui-src/misc/fonts/Roboto-Medium.ttf",
        "../_deps/imgui-src/misc/fonts/Roboto-Medium.ttf",
    };
    ImFont* font = nullptr;
    for (const char* path : paths) {
        font = io.Fonts->AddFontFromFileTTF(path, 15.0f);
        if (font) break;
    }
    if (font) {
        io.FontDefault = font;
    }
}

void ApplyTunngleTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 6);
    style.ItemInnerSpacing = ImVec2(8, 4);
    style.ScrollbarSize = 14.0f;
    style.TabRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);

    // Tunngle palette - darker, red accent
    ImVec4 bg          = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);  // near-black
    ImVec4 bgChild     = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    ImVec4 bgPopup     = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    ImVec4 border      = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    ImVec4 text        = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    ImVec4 textDim     = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    ImVec4 accent      = ImVec4(0.85f, 0.20f, 0.20f, 1.00f);   // red
    ImVec4 accentDim   = ImVec4(0.55f, 0.12f, 0.12f, 1.00f);
    ImVec4 header      = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    ImVec4 headerHov   = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    ImVec4 headerActive= ImVec4(0.45f, 0.10f, 0.10f, 1.00f);   // red tint when selected
    ImVec4 tab         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    ImVec4 tabActive   = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    ImVec4 tabHov      = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);

    ImVec4 btnTop     = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);   // raised highlight
    ImVec4 btnMid     = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);   // face
    ImVec4 btnBottom  = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);   // shadow
    ImVec4 btnHov     = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);   // hover = more highlight
    ImVec4 btnActive  = ImVec4(0.08f, 0.08f, 0.12f, 1.00f);   // pressed = recessed

    colors[ImGuiCol_WindowBg]             = bg;
    colors[ImGuiCol_ChildBg]              = bgChild;
    colors[ImGuiCol_PopupBg]              = bgPopup;
    colors[ImGuiCol_Border]               = ImVec4(0.28f, 0.28f, 0.32f, 1.0f);  // lighter = 3D highlight edge
    colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_Text]                 = text;
    colors[ImGuiCol_TextDisabled]         = textDim;
    colors[ImGuiCol_TitleBg]              = bg;
    colors[ImGuiCol_TitleBgActive]        = bg;
    colors[ImGuiCol_TitleBgCollapsed]     = bg;
    colors[ImGuiCol_MenuBarBg]            = bg;
    colors[ImGuiCol_ScrollbarBg]          = bg;
    colors[ImGuiCol_ScrollbarGrab]        = header;
    colors[ImGuiCol_ScrollbarGrabHovered] = headerHov;
    colors[ImGuiCol_ScrollbarGrabActive]   = accentDim;
    colors[ImGuiCol_CheckMark]            = accent;
    colors[ImGuiCol_SliderGrab]           = accent;
    colors[ImGuiCol_SliderGrabActive]     = accentDim;
    colors[ImGuiCol_Button]               = btnMid;
    colors[ImGuiCol_ButtonHovered]        = btnHov;
    colors[ImGuiCol_ButtonActive]         = btnActive;
    colors[ImGuiCol_Header]               = btnMid;
    colors[ImGuiCol_HeaderHovered]        = btnHov;
    colors[ImGuiCol_HeaderActive]         = btnActive;
    colors[ImGuiCol_FrameBg]              = btnMid;
    colors[ImGuiCol_FrameBgHovered]       = btnHov;
    colors[ImGuiCol_FrameBgActive]        = btnActive;
    colors[ImGuiCol_Separator]            = border;
    colors[ImGuiCol_SeparatorHovered]     = accent;
    colors[ImGuiCol_SeparatorActive]      = accent;
    colors[ImGuiCol_Tab]                      = tab;
    colors[ImGuiCol_TabHovered]               = tabHov;
    colors[ImGuiCol_TabSelected]              = tabActive;
    colors[ImGuiCol_TabSelectedOverline]     = accent;
    colors[ImGuiCol_TabDimmed]                = tab;
    colors[ImGuiCol_TabDimmedSelected]        = tabActive;
    colors[ImGuiCol_TabDimmedSelectedOverline]= accent;
    colors[ImGuiCol_TableHeaderBg]        = header;
    colors[ImGuiCol_TableBorderStrong]    = border;
    colors[ImGuiCol_TableBorderLight]    = ImVec4(0.14f, 0.14f, 0.16f, 1.0f);
    colors[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.06f, 0.06f, 0.08f, 1.0f);
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered]    = accent;
    colors[ImGuiCol_ResizeGripActive]     = accent;
}

}
