#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "GuiLauncher.h"
#include "GamePaths.h"
#include "PePatch.h"

#include "../shared/AppPaths.h"
#include "../shared/StringUtil.h"
#include "../../resources/windows/resource.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace om4t::launcher {

namespace {

// ---------------------------------------------------------------------------
// DPI scaling
// ---------------------------------------------------------------------------

int g_dpi = 96;

int dp(int value) {
    return MulDiv(value, g_dpi, 96);
}

float dpf(float value) {
    return value * static_cast<float>(g_dpi) / 96.0f;
}

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

constexpr COLORREF kColBg = RGB(0x14, 0x16, 0x1B);
constexpr COLORREF kColSidebar = RGB(0x0E, 0x10, 0x14);
constexpr COLORREF kColCard = RGB(0x1B, 0x1E, 0x26);
constexpr COLORREF kColField = RGB(0x10, 0x12, 0x17);
constexpr COLORREF kColBorder = RGB(0x2C, 0x31, 0x3C);
constexpr COLORREF kColBorderSoft = RGB(0x23, 0x27, 0x30);
constexpr COLORREF kColText = RGB(0xE8, 0xEB, 0xF1);
constexpr COLORREF kColMuted = RGB(0x8D, 0x95, 0xA3);
constexpr COLORREF kColFaint = RGB(0x5A, 0x61, 0x6E);
constexpr COLORREF kColAccent = RGB(0xF2, 0x62, 0x1F);
constexpr COLORREF kColAccentHot = RGB(0xFF, 0x7E, 0x3D);
constexpr COLORREF kColAccentDeep = RGB(0xD1, 0x4E, 0x14);
constexpr COLORREF kColWarn = RGB(0xFF, 0xB0, 0x4D);
constexpr COLORREF kColGood = RGB(0x63, 0xD2, 0x97);

Gdiplus::Color gp(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

// ---------------------------------------------------------------------------
// Small GDI+ helpers
// ---------------------------------------------------------------------------

void build_round_rect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    path.Reset();
    if (radius <= 0.5f) {
        path.AddRectangle(rect);
        return;
    }

    const float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void fill_round_rect(Gdiplus::Graphics& graphics, const Gdiplus::Brush& brush, const Gdiplus::RectF& rect, float radius) {
    Gdiplus::GraphicsPath path;
    build_round_rect(path, rect, radius);
    graphics.FillPath(&brush, &path);
}

void stroke_round_rect(Gdiplus::Graphics& graphics, COLORREF color, const Gdiplus::RectF& rect, float radius, float width = 1.0f) {
    Gdiplus::GraphicsPath path;
    build_round_rect(path, Gdiplus::RectF(rect.X + 0.5f, rect.Y + 0.5f, rect.Width - 1.0f, rect.Height - 1.0f), radius);
    Gdiplus::Pen pen(gp(color), width);
    graphics.DrawPath(&pen, &path);
}

void fill_vertical_gradient(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, COLORREF top, COLORREF bottom, float radius) {
    Gdiplus::LinearGradientBrush brush(
        Gdiplus::RectF(rect.X, rect.Y - 1.0f, rect.Width, rect.Height + 2.0f),
        gp(top),
        gp(bottom),
        Gdiplus::LinearGradientModeVertical);
    fill_round_rect(graphics, brush, rect, radius);
}

Gdiplus::RectF to_rectf(const RECT& rect) {
    return Gdiplus::RectF(
        static_cast<float>(rect.left),
        static_cast<float>(rect.top),
        static_cast<float>(rect.right - rect.left),
        static_cast<float>(rect.bottom - rect.top));
}

// The logo asset is a square canvas with generous transparent padding. These are
// the bounds of the artwork inside it, so the launcher can lay it out tightly.
constexpr float kLogoSrcX = 20.0f;
constexpr float kLogoSrcY = 232.0f;
constexpr float kLogoSrcW = 1000.0f;
constexpr float kLogoSrcH = 588.0f;
constexpr float kLogoAspect = kLogoSrcW / kLogoSrcH;

void draw_logo(Gdiplus::Graphics& graphics, Gdiplus::Bitmap* logo, float x, float y, float width) {
    if (!logo) {
        return;
    }
    const Gdiplus::RectF destination(x, y, width, width / kLogoAspect);
    graphics.DrawImage(logo, destination, kLogoSrcX, kLogoSrcY, kLogoSrcW, kLogoSrcH, Gdiplus::UnitPixel);
}

std::unique_ptr<Gdiplus::Bitmap> load_png_resource(HINSTANCE instance, int resource_id) {
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource) {
        return {};
    }

    HGLOBAL loaded = LoadResource(instance, resource);
    const DWORD size = SizeofResource(instance, resource);
    const void* data = LockResource(loaded);
    if (!loaded || !data || size == 0) {
        return {};
    }

    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!copy) {
        return {};
    }

    void* copy_data = GlobalLock(copy);
    if (!copy_data) {
        GlobalFree(copy);
        return {};
    }
    CopyMemory(copy_data, data, size);
    GlobalUnlock(copy);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(copy, TRUE, &stream))) {
        GlobalFree(copy);
        return {};
    }

    std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromStream(stream));
    stream->Release();
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        return {};
    }
    return bitmap;
}

// ---------------------------------------------------------------------------
// Shared launcher state
// ---------------------------------------------------------------------------

constexpr int kTweakBase = 4000;
constexpr int kSettingBase = 5000;

struct SettingControl {
    size_t package_index = 0;
    size_t setting_index = 0;
    HWND control = nullptr;
};

struct GuiState {
    Config* target_config = nullptr;
    std::vector<TweakPackage>* target_packages = nullptr;
    std::filesystem::path config_path;
    std::filesystem::path root;
    std::unique_ptr<Gdiplus::Bitmap> logo;
    HFONT font_ui = nullptr;
    HFONT font_small = nullptr;
    HFONT font_section = nullptr;
    HFONT font_title = nullptr;
    HFONT font_nav = nullptr;
};

std::wstring window_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length), L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    return text;
}

HFONT make_font(int point_size, int weight = FW_NORMAL) {
    HDC dc = GetDC(nullptr);
    const int height = -MulDiv(point_size, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, dc);
    return CreateFontW(
        height,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
}

size_t enabled_count(const std::vector<TweakPackage>& packages) {
    size_t count = 0;
    for (const auto& package : packages) {
        if (package.enabled) {
            ++count;
        }
    }
    return count;
}

void apply_dark_titlebar(HWND window) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) {
        return;
    }

    using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto set_attribute = reinterpret_cast<SetAttribute>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (set_attribute) {
        BOOL dark = TRUE;
        // 20 on current Windows 10/11, 19 on the first builds that shipped it.
        if (FAILED(set_attribute(window, 20, &dark, sizeof(dark)))) {
            set_attribute(window, 19, &dark, sizeof(dark));
        }
    }

    FreeLibrary(dwm);
}

// ===========================================================================
// Splash screen
// ===========================================================================

constexpr int kSplashCardW = 420;
constexpr int kSplashCardH = 496;
constexpr int kSplashMargin = 20;
constexpr int kSplashHeroH = 188;
constexpr int kSplashButtonH = 62;
constexpr int kSplashButtonGap = 12;

enum class SplashAction {
    None,
    Play,
    Editor,
    Settings,
    Quit
};

struct SplashButton {
    SplashAction action = SplashAction::None;
    RECT rect{};
    std::wstring title;
    std::wstring subtitle;
    bool primary = false;
    bool enabled = true;
};

struct SplashState {
    GuiState* app = nullptr;
    HWND window = nullptr;
    std::vector<SplashButton> buttons;
    RECT close_rect{};
    int hovered = -1;
    int pressed = -1;
    bool close_hovered = false;
    bool tracking = false;
    SplashAction action = SplashAction::Quit;
    std::wstring status;
    COLORREF status_color = kColMuted;
};

void build_splash_layout(SplashState& state) {
    const auto& config = *state.app->target_config;
    const auto& packages = *state.app->target_packages;
    // The editor is the game executable started with "-editor", so both entries
    // depend on the same file being present.
    const bool has_game = file_exists(config.em4_path);

    state.buttons.clear();

    const int left = dp(kSplashMargin);
    const int top = dp(kSplashMargin);
    const int width = dp(kSplashCardW);

    const int button_x = left + dp(28);
    const int button_w = width - dp(56);
    int y = top + dp(kSplashHeroH) + dp(24);

    const auto push = [&](SplashAction action, std::wstring title, std::wstring subtitle, bool primary, bool enabled) {
        SplashButton button;
        button.action = action;
        button.rect = RECT{button_x, y, button_x + button_w, y + dp(kSplashButtonH)};
        button.title = std::move(title);
        button.subtitle = std::move(subtitle);
        button.primary = primary;
        button.enabled = enabled;
        state.buttons.push_back(std::move(button));
        y += dp(kSplashButtonH) + dp(kSplashButtonGap);
    };

    push(SplashAction::Play,
         L"Play Game",
         has_game ? L"Start EMERGENCY 4 with your tweaks" : L"em4.exe not found — check Tweak Settings",
         true,
         has_game);
    push(SplashAction::Editor,
         L"Open Editor",
         has_game ? L"Start EMERGENCY 4 in editor mode" : L"em4.exe not found — check Tweak Settings",
         false,
         has_game);
    push(SplashAction::Settings,
         L"Tweak Settings",
         L"Enable tweaks, set paths, 4 GB patch",
         false,
         true);

    state.close_rect = RECT{left + width - dp(42), top + dp(10), left + width - dp(10), top + dp(42)};

    if (!has_game) {
        state.status = L"EMERGENCY 4 executable not found";
        state.status_color = kColWarn;
    } else {
        const size_t enabled = enabled_count(packages);
        state.status = std::to_wstring(enabled) + L" of " + std::to_wstring(packages.size()) + L" tweaks enabled";
        state.status_color = enabled > 0 ? kColGood : kColMuted;
    }
}

void draw_chevron(Gdiplus::Graphics& graphics, float x, float y, float size, COLORREF color) {
    Gdiplus::Pen pen(gp(color), dpf(1.8f));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawLine(&pen, x, y - size, x + size, y);
    graphics.DrawLine(&pen, x + size, y, x, y + size);
}

void draw_splash_button(
    Gdiplus::Graphics& graphics,
    const SplashButton& button,
    bool hovered,
    bool pressed,
    const Gdiplus::FontFamily& family) {
    const Gdiplus::RectF rect = to_rectf(button.rect);
    const float radius = dpf(10.0f);

    COLORREF title_color = kColText;
    COLORREF subtitle_color = kColMuted;
    COLORREF chevron_color = kColMuted;

    if (!button.enabled) {
        fill_vertical_gradient(graphics, rect, RGB(0x17, 0x19, 0x1F), RGB(0x14, 0x16, 0x1B), radius);
        stroke_round_rect(graphics, kColBorderSoft, rect, radius);
        title_color = kColFaint;
        subtitle_color = RGB(0x4B, 0x51, 0x5C);
        chevron_color = RGB(0x3A, 0x3F, 0x49);
    } else if (button.primary) {
        const COLORREF top = pressed ? kColAccentDeep : (hovered ? kColAccentHot : kColAccent);
        const COLORREF bottom = pressed ? RGB(0xB8, 0x43, 0x10) : (hovered ? kColAccent : kColAccentDeep);
        fill_vertical_gradient(graphics, rect, top, bottom, radius);
        title_color = RGB(0xFF, 0xFF, 0xFF);
        subtitle_color = RGB(0xFF, 0xE2, 0xD2);
        chevron_color = RGB(0xFF, 0xE2, 0xD2);
    } else {
        const COLORREF top = pressed ? RGB(0x1C, 0x20, 0x28) : (hovered ? RGB(0x2A, 0x30, 0x3B) : RGB(0x23, 0x27, 0x31));
        const COLORREF bottom = pressed ? RGB(0x18, 0x1B, 0x22) : (hovered ? RGB(0x22, 0x27, 0x31) : RGB(0x1B, 0x1F, 0x27));
        fill_vertical_gradient(graphics, rect, top, bottom, radius);
        stroke_round_rect(graphics, hovered ? RGB(0x44, 0x4C, 0x5B) : kColBorder, rect, radius);
    }

    Gdiplus::Font title_font(&family, dpf(15.0f), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font subtitle_font(&family, dpf(11.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    const float text_x = rect.X + dpf(20.0f);
    const float text_w = rect.Width - dpf(56.0f);

    Gdiplus::SolidBrush title_brush(gp(title_color));
    Gdiplus::SolidBrush subtitle_brush(gp(subtitle_color));
    graphics.DrawString(
        button.title.c_str(), -1, &title_font,
        Gdiplus::RectF(text_x, rect.Y + dpf(11.0f), text_w, dpf(20.0f)), &format, &title_brush);
    graphics.DrawString(
        button.subtitle.c_str(), -1, &subtitle_font,
        Gdiplus::RectF(text_x, rect.Y + dpf(34.0f), text_w, dpf(18.0f)), &format, &subtitle_brush);

    const float chevron_x = rect.GetRight() - dpf(26.0f) + (hovered && button.enabled ? dpf(3.0f) : 0.0f);
    draw_chevron(graphics, chevron_x, rect.Y + rect.Height / 2.0f, dpf(5.0f), chevron_color);
}

void render_splash(SplashState& state) {
    if (!state.window) {
        return;
    }

    const int window_w = dp(kSplashCardW + kSplashMargin * 2);
    const int window_h = dp(kSplashCardH + kSplashMargin * 2);

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = window_w;
    info.bmiHeader.biHeight = -window_h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old_bitmap = SelectObject(memory, bitmap);

    {
        Gdiplus::Graphics graphics(memory);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

        const Gdiplus::RectF card(
            static_cast<float>(dp(kSplashMargin)),
            static_cast<float>(dp(kSplashMargin)),
            static_cast<float>(dp(kSplashCardW)),
            static_cast<float>(dp(kSplashCardH)));
        const float card_radius = dpf(16.0f);

        // Soft drop shadow, built from nested translucent rounded rectangles.
        const int rings = dp(kSplashMargin) - dp(2);
        for (int i = rings; i >= 1; --i) {
            const float grow = static_cast<float>(i);
            const Gdiplus::RectF ring(card.X - grow, card.Y - grow + dpf(2.0f), card.Width + grow * 2.0f, card.Height + grow * 2.0f);
            Gdiplus::SolidBrush brush(Gdiplus::Color(7, 0, 0, 0));
            fill_round_rect(graphics, brush, ring, card_radius + grow);
        }

        Gdiplus::GraphicsPath card_path;
        build_round_rect(card_path, card, card_radius);
        fill_vertical_gradient(graphics, card, RGB(0x1D, 0x21, 0x2A), RGB(0x0F, 0x11, 0x16), card_radius);

        graphics.SetClip(&card_path);

        // Hero band behind the logo.
        const Gdiplus::RectF hero(card.X, card.Y, card.Width, static_cast<float>(dp(kSplashHeroH)));
        Gdiplus::LinearGradientBrush hero_brush(
            Gdiplus::RectF(hero.X, hero.Y - 1.0f, hero.Width, hero.Height + 2.0f),
            gp(RGB(0x26, 0x2B, 0x35)),
            gp(RGB(0x15, 0x18, 0x1E)),
            Gdiplus::LinearGradientModeVertical);
        graphics.FillRectangle(&hero_brush, hero);

        // Accent hairline separating the hero from the actions.
        {
            Gdiplus::LinearGradientBrush line_brush(
                Gdiplus::RectF(hero.X, hero.GetBottom() - 1.0f, hero.Width, dpf(3.0f)),
                gp(RGB(0x15, 0x18, 0x1E)),
                gp(RGB(0x15, 0x18, 0x1E)),
                Gdiplus::LinearGradientModeHorizontal);
            Gdiplus::Color colors[3]{gp(RGB(0x15, 0x18, 0x1E)), gp(kColAccent), gp(RGB(0x15, 0x18, 0x1E))};
            Gdiplus::REAL positions[3]{0.0f, 0.5f, 1.0f};
            line_brush.SetInterpolationColors(colors, positions, 3);
            graphics.FillRectangle(&line_brush, Gdiplus::RectF(hero.X, hero.GetBottom() - dpf(2.0f), hero.Width, dpf(2.0f)));
        }

        const float logo_w = card.Width - dpf(120.0f);
        draw_logo(
            graphics,
            state.app->logo.get(),
            card.X + (card.Width - logo_w) / 2.0f,
            card.Y + (hero.Height - logo_w / kLogoAspect) / 2.0f - dpf(4.0f),
            logo_w);

        graphics.ResetClip();

        Gdiplus::FontFamily family(L"Segoe UI");
        for (size_t i = 0; i < state.buttons.size(); ++i) {
            draw_splash_button(
                graphics,
                state.buttons[i],
                state.hovered == static_cast<int>(i),
                state.pressed == static_cast<int>(i),
                family);
        }

        // Footer.
        const float footer_y = card.GetBottom() - dpf(46.0f);
        Gdiplus::SolidBrush divider(gp(RGB(0x25, 0x2A, 0x34)));
        graphics.FillRectangle(&divider, Gdiplus::RectF(card.X + dpf(28.0f), footer_y, card.Width - dpf(56.0f), 1.0f));

        Gdiplus::Font footer_font(&family, dpf(11.5f), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat center;
        center.SetAlignment(Gdiplus::StringAlignmentCenter);
        center.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        center.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        Gdiplus::SolidBrush footer_brush(gp(state.status_color));
        graphics.DrawString(
            state.status.c_str(), -1, &footer_font,
            Gdiplus::RectF(card.X + dpf(20.0f), footer_y + dpf(10.0f), card.Width - dpf(40.0f), dpf(24.0f)),
            &center, &footer_brush);

        // Close button.
        const Gdiplus::RectF close = to_rectf(state.close_rect);
        if (state.close_hovered) {
            Gdiplus::SolidBrush hover(gp(RGB(0x30, 0x36, 0x42)));
            fill_round_rect(graphics, hover, close, dpf(8.0f));
        }
        Gdiplus::Pen close_pen(gp(state.close_hovered ? kColText : kColFaint), dpf(1.6f));
        close_pen.SetStartCap(Gdiplus::LineCapRound);
        close_pen.SetEndCap(Gdiplus::LineCapRound);
        const float inset = dpf(11.0f);
        graphics.DrawLine(&close_pen, close.X + inset, close.Y + inset, close.GetRight() - inset, close.GetBottom() - inset);
        graphics.DrawLine(&close_pen, close.GetRight() - inset, close.Y + inset, close.X + inset, close.GetBottom() - inset);
    }

    POINT source{0, 0};
    SIZE size{window_w, window_h};
    RECT window_rect{};
    GetWindowRect(state.window, &window_rect);
    POINT destination{window_rect.left, window_rect.top};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(state.window, screen, &destination, &size, memory, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

int splash_hit_test(const SplashState& state, POINT point) {
    for (size_t i = 0; i < state.buttons.size(); ++i) {
        if (PtInRect(&state.buttons[i].rect, point)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void finish_splash(SplashState& state, SplashAction action) {
    state.action = action;
    DestroyWindow(state.window);
}

LRESULT CALLBACK splash_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<SplashState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE:
        state = reinterpret_cast<SplashState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        state->window = window;
        build_splash_layout(*state);
        render_splash(*state);
        return 0;
    case WM_MOUSEMOVE: {
        if (!state) {
            break;
        }
        if (!state->tracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            state->tracking = true;
        }

        const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const int hovered = splash_hit_test(*state, point);
        const bool close_hovered = PtInRect(&state->close_rect, point) != FALSE;
        if (hovered != state->hovered || close_hovered != state->close_hovered) {
            state->hovered = hovered;
            state->close_hovered = close_hovered;
            SetCursor(LoadCursorW(nullptr, (hovered >= 0 || close_hovered) ? IDC_HAND : IDC_ARROW));
            render_splash(*state);
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (state && (state->hovered >= 0 || state->close_hovered)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_MOUSELEAVE:
        if (state) {
            state->tracking = false;
            state->hovered = -1;
            state->pressed = -1;
            state->close_hovered = false;
            render_splash(*state);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        if (!state) {
            break;
        }
        const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const int hit = splash_hit_test(*state, point);
        if (hit >= 0) {
            state->pressed = hit;
            SetCapture(window);
            render_splash(*state);
        } else if (!PtInRect(&state->close_rect, point)) {
            // Anywhere else on the card drags the window.
            ReleaseCapture();
            SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!state) {
            break;
        }
        const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const int pressed = state->pressed;
        if (pressed >= 0) {
            ReleaseCapture();
            state->pressed = -1;
            render_splash(*state);
            if (splash_hit_test(*state, point) == pressed && state->buttons[pressed].enabled) {
                finish_splash(*state, state->buttons[pressed].action);
                return 0;
            }
        }
        if (PtInRect(&state->close_rect, point)) {
            finish_splash(*state, SplashAction::Quit);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (!state) {
            break;
        }
        if (wparam == VK_ESCAPE) {
            finish_splash(*state, SplashAction::Quit);
            return 0;
        }
        if (wparam == VK_RETURN && !state->buttons.empty() && state->buttons[0].enabled) {
            finish_splash(*state, SplashAction::Play);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (state) {
            finish_splash(*state, SplashAction::Quit);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

// ===========================================================================
// Settings window
// ===========================================================================

constexpr int kWinW = 880;
constexpr int kWinH = 640;
constexpr int kSidebarW = 216;
constexpr int kActionBarH = 68;
constexpr int kPageTop = 100;
constexpr int kCardMargin = 32;
constexpr int kScrollLine = 34;

constexpr int kIdNavBase = 3000;
constexpr int kIdBrowseEm4 = 3010;
constexpr int kIdPatch4Gb = 3020;
constexpr int kIdRestore4Gb = 3021;
constexpr int kIdOpenLogs = 3030;
constexpr int kIdOpenConfig = 3031;
constexpr int kIdOpenHooks = 3032;
constexpr int kIdSaveLaunch = 3040;
constexpr int kIdSaveClose = 3041;
constexpr int kIdCancel = 3042;
constexpr int kIdEm4Edit = 3050;

enum class SettingsResult {
    Launch,
    Close,
    Cancel
};

enum class ButtonKind {
    Primary,
    Secondary,
    Ghost,
    Nav,
    Toggle
};

struct ButtonInfo {
    ButtonKind kind = ButtonKind::Secondary;
    bool hovered = false;
    bool checked = false;
};

struct SettingsState;

struct PageState {
    SettingsState* owner = nullptr;
    int index = 0;
    int scroll = 0;
    int content_height = 0;
    std::vector<RECT> cards;
    std::vector<RECT> fields;
};

struct SettingsState {
    GuiState* app = nullptr;
    Config draft_config;
    std::vector<TweakPackage> draft_packages;
    HWND window = nullptr;
    HWND pages[3]{};
    HWND nav[3]{};
    HWND em4_edit = nullptr;
    HWND lai_status = nullptr;
    HWND patch_button = nullptr;
    HWND restore_button = nullptr;
    HWND sidebar_note = nullptr;
    PageState page_state[3];
    std::vector<HWND> tweak_toggles;
    std::vector<SettingControl> setting_controls;
    std::map<HWND, ButtonInfo> buttons;
    std::map<HWND, std::wstring> button_labels;
    HBRUSH brush_bg = nullptr;
    HBRUSH brush_card = nullptr;
    HBRUSH brush_field = nullptr;
    HBRUSH brush_sidebar = nullptr;
    int active_page = 0;
    SettingsResult result = SettingsResult::Cancel;
};

const wchar_t* const kPageTitles[3]{L"General", L"Tweaks", L"About"};
const wchar_t* const kPageSubtitles[3]{
    L"Where EMERGENCY 4 lives and how much memory it may use.",
    L"Turn tweak packages on or off and adjust their settings.",
    L"Package details and handy shortcuts."};
const wchar_t* const kNavLabels[3]{L"General", L"Tweaks", L"About"};

int page_height() {
    return dp(kWinH - kActionBarH) - dp(kPageTop);
}

// --- owner drawn controls --------------------------------------------------

LRESULT CALLBACK button_subclass(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR ref) {
    auto* info = reinterpret_cast<ButtonInfo*>(ref);

    switch (message) {
    case WM_MOUSEMOVE:
        if (info && !info->hovered) {
            info->hovered = true;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            InvalidateRect(window, nullptr, TRUE);
        }
        break;
    case WM_MOUSELEAVE:
        if (info && info->hovered) {
            info->hovered = false;
            InvalidateRect(window, nullptr, TRUE);
        }
        break;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IsWindowEnabled(window) ? IDC_HAND : IDC_ARROW));
        return TRUE;
    case WM_NCDESTROY:
        RemoveWindowSubclass(window, button_subclass, 1);
        break;
    }

    return DefSubclassProc(window, message, wparam, lparam);
}

HWND make_button(
    SettingsState& state,
    HWND parent,
    const std::wstring& text,
    int id,
    int x,
    int y,
    int w,
    int h,
    ButtonKind kind) {
    HWND button = CreateWindowExW(
        0,
        L"BUTTON",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        GetModuleHandleW(nullptr),
        nullptr);

    ButtonInfo& info = state.buttons[button];
    info.kind = kind;
    state.button_labels[button] = text;
    SetWindowSubclass(button, button_subclass, 1, reinterpret_cast<DWORD_PTR>(&info));
    return button;
}

HWND make_label(HWND parent, const std::wstring& text, HFONT font, int x, int y, int w, int h, bool muted, bool wrap = false) {
    HWND label = CreateWindowExW(
        0,
        L"STATIC",
        text.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | (wrap ? 0u : static_cast<unsigned>(SS_ENDELLIPSIS)),
        x,
        y,
        w,
        h,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SetWindowLongPtrW(label, GWLP_USERDATA, muted ? 1 : 0);
    return label;
}

HWND make_edit(HWND parent, const std::wstring& text, HFONT font, int id, int x, int y, int w, int h, bool numeric) {
    HWND edit = CreateWindowExW(
        0,
        L"EDIT",
        text.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (numeric ? ES_NUMBER : 0u),
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(dp(8), dp(8)));
    return edit;
}

void draw_toggle(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, bool checked, bool hovered, bool enabled) {
    const float height = rect.Height;
    const float radius = height / 2.0f;
    const Gdiplus::RectF track(rect.X, rect.Y, rect.Width, height);

    COLORREF fill = checked ? (hovered ? kColAccentHot : kColAccent) : (hovered ? RGB(0x3A, 0x41, 0x4E) : RGB(0x2E, 0x34, 0x40));
    if (!enabled) {
        fill = RGB(0x24, 0x28, 0x30);
    }

    Gdiplus::SolidBrush track_brush(gp(fill));
    fill_round_rect(graphics, track_brush, track, radius);
    if (!checked) {
        stroke_round_rect(graphics, enabled ? kColBorder : kColBorderSoft, track, radius);
    }

    const float knob_size = height - dpf(6.0f);
    const float knob_x = checked ? track.GetRight() - knob_size - dpf(3.0f) : track.X + dpf(3.0f);
    Gdiplus::SolidBrush knob_brush(gp(enabled ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x6A, 0x71, 0x7E)));
    graphics.FillEllipse(&knob_brush, Gdiplus::RectF(knob_x, track.Y + dpf(3.0f), knob_size, knob_size));
}

void draw_owner_button(SettingsState& state, const DRAWITEMSTRUCT* item) {
    auto entry = state.buttons.find(item->hwndItem);
    if (entry == state.buttons.end()) {
        return;
    }

    const ButtonInfo& info = entry->second;
    const bool enabled = (item->itemState & ODS_DISABLED) == 0;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool focused = (item->itemState & ODS_FOCUS) != 0;
    const Gdiplus::RectF rect = to_rectf(item->rcItem);

    // Owner drawn buttons never erase themselves, so start from the parent colour.
    COLORREF backdrop = kColCard;
    if (info.kind == ButtonKind::Nav) {
        backdrop = kColSidebar;
    } else if (info.kind == ButtonKind::Primary || info.kind == ButtonKind::Secondary || info.kind == ButtonKind::Ghost) {
        backdrop = GetParent(item->hwndItem) == state.window ? kColSidebar : kColCard;
    }
    HBRUSH backdrop_brush = CreateSolidBrush(backdrop);
    FillRect(item->hDC, &item->rcItem, backdrop_brush);
    DeleteObject(backdrop_brush);

    Gdiplus::Graphics graphics(item->hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    if (info.kind == ButtonKind::Toggle) {
        draw_toggle(graphics, rect, info.checked, info.hovered, enabled);
        if (focused) {
            stroke_round_rect(graphics, kColAccentHot, rect, rect.Height / 2.0f);
        }
        return;
    }

    const float radius = dpf(info.kind == ButtonKind::Nav ? 8.0f : 7.0f);
    COLORREF text_color = kColText;

    switch (info.kind) {
    case ButtonKind::Primary: {
        const COLORREF top = !enabled ? RGB(0x3A, 0x2C, 0x24) : pressed ? kColAccentDeep : (info.hovered ? kColAccentHot : kColAccent);
        const COLORREF bottom = !enabled ? RGB(0x30, 0x25, 0x1F) : pressed ? RGB(0xB8, 0x43, 0x10) : (info.hovered ? kColAccent : kColAccentDeep);
        fill_vertical_gradient(graphics, rect, top, bottom, radius);
        text_color = enabled ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x8A, 0x77, 0x6C);
        break;
    }
    case ButtonKind::Secondary: {
        const COLORREF solid = !enabled ? RGB(0x1B, 0x1E, 0x24) : pressed ? RGB(0x1E, 0x22, 0x2A) : (info.hovered ? RGB(0x30, 0x36, 0x42) : RGB(0x26, 0x2B, 0x35));
        Gdiplus::SolidBrush brush(gp(solid));
        fill_round_rect(graphics, brush, rect, radius);
        stroke_round_rect(graphics, enabled ? (info.hovered ? RGB(0x44, 0x4C, 0x5B) : kColBorder) : kColBorderSoft, rect, radius);
        text_color = enabled ? kColText : kColFaint;
        break;
    }
    case ButtonKind::Ghost: {
        if (info.hovered && enabled) {
            Gdiplus::SolidBrush brush(gp(RGB(0x26, 0x2B, 0x35)));
            fill_round_rect(graphics, brush, rect, radius);
        }
        text_color = !enabled ? kColFaint : (info.hovered ? kColText : kColMuted);
        break;
    }
    case ButtonKind::Nav: {
        if (info.checked) {
            Gdiplus::SolidBrush brush(gp(kColAccent, 34));
            fill_round_rect(graphics, brush, rect, radius);
            Gdiplus::SolidBrush bar(gp(kColAccent));
            fill_round_rect(graphics, bar, Gdiplus::RectF(rect.X, rect.Y + dpf(9.0f), dpf(3.0f), rect.Height - dpf(18.0f)), dpf(1.5f));
            text_color = kColText;
        } else if (info.hovered) {
            Gdiplus::SolidBrush brush(gp(RGB(0x1B, 0x1F, 0x27)));
            fill_round_rect(graphics, brush, rect, radius);
            text_color = kColText;
        } else {
            text_color = kColMuted;
        }
        break;
    }
    default:
        break;
    }

    if (focused && info.kind != ButtonKind::Nav) {
        stroke_round_rect(graphics, kColAccentHot, rect, radius);
    }

    const auto label = state.button_labels.find(item->hwndItem);
    if (label == state.button_labels.end()) {
        return;
    }

    HFONT font = info.kind == ButtonKind::Nav ? state.app->font_nav : state.app->font_ui;
    HGDIOBJ old_font = SelectObject(item->hDC, font);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, text_color);

    RECT text_rect = item->rcItem;
    UINT format = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
    if (info.kind == ButtonKind::Nav) {
        text_rect.left += dp(18);
        format |= DT_LEFT;
    } else {
        format |= DT_CENTER;
    }
    DrawTextW(item->hDC, label->second.c_str(), -1, &text_rect, format);
    SelectObject(item->hDC, old_font);
}

// --- page scrolling --------------------------------------------------------

void update_scroll_info(PageState& page, HWND window) {
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, page.content_height - 1);
    info.nPage = static_cast<UINT>(page_height());
    info.nPos = page.scroll;
    SetScrollInfo(window, SB_VERT, &info, TRUE);
}

void scroll_page(PageState& page, HWND window, int position) {
    const int limit = std::max(0, page.content_height - page_height());
    position = std::clamp(position, 0, limit);
    const int delta = page.scroll - position;
    if (delta == 0) {
        return;
    }

    page.scroll = position;
    ScrollWindowEx(window, 0, delta, nullptr, nullptr, nullptr, nullptr, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    update_scroll_info(page, window);
    UpdateWindow(window);
}

void paint_page(PageState& page, HWND window, HDC dc) {
    RECT client{};
    GetClientRect(window, &client);

    HBRUSH background = CreateSolidBrush(kColBg);
    FillRect(dc, &client, background);
    DeleteObject(background);

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    // Cards are filled flat so that the brush handed back from WM_CTLCOLORSTATIC
    // matches what is painted underneath the labels.
    Gdiplus::SolidBrush card_brush(gp(kColCard));
    for (const RECT& base : page.cards) {
        Gdiplus::RectF rect = to_rectf(base);
        rect.Y -= static_cast<float>(page.scroll);
        fill_round_rect(graphics, card_brush, rect, dpf(12.0f));
        stroke_round_rect(graphics, kColBorder, rect, dpf(12.0f));
    }

    for (const RECT& base : page.fields) {
        Gdiplus::RectF rect = to_rectf(base);
        rect.Y -= static_cast<float>(page.scroll);
        Gdiplus::SolidBrush brush(gp(kColField));
        fill_round_rect(graphics, brush, rect, dpf(6.0f));
        stroke_round_rect(graphics, kColBorder, rect, dpf(6.0f));
    }
}

LRESULT CALLBACK page_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* page = reinterpret_cast<PageState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (page) {
            paint_page(*page, window, dc);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        if (!page) {
            break;
        }
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetWindowLongPtrW(reinterpret_cast<HWND>(lparam), GWLP_USERDATA) == 1 ? kColMuted : kColText);
        return reinterpret_cast<LRESULT>(page->owner->brush_card);
    }
    case WM_CTLCOLOREDIT: {
        if (!page) {
            break;
        }
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, kColField);
        SetTextColor(dc, kColText);
        return reinterpret_cast<LRESULT>(page->owner->brush_field);
    }
    case WM_DRAWITEM:
        if (page) {
            draw_owner_button(*page->owner, reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (page) {
            return SendMessageW(page->owner->window, WM_COMMAND, wparam, lparam);
        }
        break;
    case WM_VSCROLL: {
        if (!page) {
            break;
        }
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_ALL;
        GetScrollInfo(window, SB_VERT, &info);

        int position = page->scroll;
        switch (LOWORD(wparam)) {
        case SB_LINEUP:
            position -= dp(kScrollLine);
            break;
        case SB_LINEDOWN:
            position += dp(kScrollLine);
            break;
        case SB_PAGEUP:
            position -= page_height() - dp(40);
            break;
        case SB_PAGEDOWN:
            position += page_height() - dp(40);
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            position = info.nTrackPos;
            break;
        case SB_TOP:
            position = 0;
            break;
        case SB_BOTTOM:
            position = page->content_height;
            break;
        }
        scroll_page(*page, window, position);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (page) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            scroll_page(*page, window, page->scroll - delta / WHEEL_DELTA * dp(kScrollLine) * 2);
            return 0;
        }
        break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

// --- layout ----------------------------------------------------------------

int measure_text_height(HWND reference, HFONT font, const std::wstring& text, int width) {
    HDC dc = GetDC(reference);
    HGDIOBJ old_font = SelectObject(dc, font);
    RECT rect{0, 0, width, 0};
    DrawTextW(dc, text.c_str(), -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old_font);
    ReleaseDC(reference, dc);
    return rect.bottom - rect.top;
}

std::wstring setting_value(const Config& config, const TweakSetting& setting) {
    if (auto section = config.sections.find(setting.section); section != config.sections.end()) {
        if (auto value = section->second.find(setting.key); value != section->second.end()) {
            return value->second;
        }
    }
    return setting.default_value;
}

std::filesystem::path current_em4_path(const SettingsState& state) {
    std::filesystem::path path = trim(window_text(state.em4_edit));
    if (path.empty()) {
        return {};
    }
    if (path.is_relative()) {
        path = state.app->root / path;
    }
    return std::filesystem::absolute(path);
}

void update_large_address_status(SettingsState& state) {
    if (!state.lai_status) {
        return;
    }

    const auto info = inspect_large_address_aware(current_em4_path(state));
    std::wstring status = info.message;
    if (info.backup_exists) {
        status += L"  Backup available.";
    }

    SetWindowTextW(state.lai_status, status.c_str());
    EnableWindow(state.patch_button, info.state == LargeAddressAwareState::Disabled);
    EnableWindow(state.restore_button, info.backup_exists);
    InvalidateRect(state.patch_button, nullptr, TRUE);
    InvalidateRect(state.restore_button, nullptr, TRUE);
}

void build_general_page(SettingsState& state) {
    HWND page = state.pages[0];
    PageState& info = state.page_state[0];
    GuiState& app = *state.app;

    const int card_x = dp(kCardMargin);
    const int card_w = dp(kWinW - kSidebarW - kCardMargin * 2);
    const int pad = dp(20);
    int y = dp(4);

    // --- game path card ---
    {
        const int card_top = y;
        int inner = y + pad;

        make_label(page, L"Game executable", app.font_section, card_x + pad, inner, card_w - pad * 2, dp(20), false);
        inner += dp(30);

        make_label(page, L"The EMERGENCY 4 executable the launcher starts.", app.font_small, card_x + pad, inner, card_w - pad * 2, dp(18), true);
        inner += dp(22);

        const int browse_w = dp(96);
        const int edit_w = card_w - pad * 2 - browse_w - dp(10);
        state.em4_edit = make_edit(page, state.draft_config.em4_path.wstring(), app.font_ui, kIdEm4Edit, card_x + pad + dp(1), inner + dp(1), edit_w - dp(2), dp(30), false);
        info.fields.push_back(RECT{card_x + pad, inner, card_x + pad + edit_w, inner + dp(32)});
        make_button(state, page, L"Browse", kIdBrowseEm4, card_x + pad + edit_w + dp(10), inner, browse_w, dp(32), ButtonKind::Secondary);
        inner += dp(44);

        make_label(
            page,
            L"Open Editor runs this same executable with -editor. Tweaks are not injected into the editor.",
            app.font_small,
            card_x + pad,
            inner,
            card_w - pad * 2,
            dp(18),
            true);
        inner += dp(22) + pad - dp(4);

        info.cards.push_back(RECT{card_x, card_top, card_x + card_w, inner});
        y = inner + dp(20);
    }

    // --- 4 GB patch card ---
    {
        const int card_top = y;
        int inner = y + pad;

        make_label(page, L"4 GB patch helper", app.font_section, card_x + pad, inner, card_w - pad * 2, dp(20), false);
        inner += dp(28);

        const std::wstring blurb = L"Large Address Aware lets 32-bit EMERGENCY 4 use more memory on modern Windows. "
                                   L"The original executable is backed up before it is patched.";
        const int blurb_h = measure_text_height(page, app.font_small, blurb, card_w - pad * 2);
        make_label(page, blurb, app.font_small, card_x + pad, inner, card_w - pad * 2, blurb_h, true, true);
        inner += blurb_h + dp(12);

        state.lai_status = make_label(page, L"", app.font_ui, card_x + pad, inner, card_w - pad * 2, dp(22), false);
        inner += dp(32);

        state.patch_button = make_button(state, page, L"Apply 4 GB patch", kIdPatch4Gb, card_x + pad, inner, dp(150), dp(34), ButtonKind::Secondary);
        state.restore_button = make_button(state, page, L"Restore backup", kIdRestore4Gb, card_x + pad + dp(160), inner, dp(140), dp(34), ButtonKind::Ghost);
        inner += dp(34) + pad;

        info.cards.push_back(RECT{card_x, card_top, card_x + card_w, inner});
        y = inner + dp(20);
    }

    info.content_height = y;
}

void build_tweaks_page(SettingsState& state) {
    HWND page = state.pages[1];
    PageState& info = state.page_state[1];
    GuiState& app = *state.app;

    const int card_x = dp(kCardMargin);
    const int card_w = dp(kWinW - kSidebarW - kCardMargin * 2);
    const int pad = dp(20);
    const int toggle_w = dp(44);
    const int toggle_h = dp(24);
    int y = dp(4);

    if (state.draft_packages.empty()) {
        const int card_top = y;
        make_label(page, L"No tweak packages found", app.font_section, card_x + pad, y + pad, card_w - pad * 2, dp(20), false);
        make_label(
            page,
            L"Install packages under OMoutaEM4Tweaks\\Hooks and restart the launcher.",
            app.font_small,
            card_x + pad,
            y + pad + dp(28),
            card_w - pad * 2,
            dp(18),
            true);
        info.cards.push_back(RECT{card_x, card_top, card_x + card_w, y + pad * 2 + dp(46)});
        info.content_height = y + pad * 2 + dp(66);
        return;
    }

    for (size_t i = 0; i < state.draft_packages.size(); ++i) {
        auto& package = state.draft_packages[i];
        const int card_top = y;
        int inner = y + pad;

        make_label(page, package.name, app.font_section, card_x + pad, inner, card_w - pad * 2 - toggle_w - dp(12), dp(20), false);
        HWND toggle = make_button(
            state,
            page,
            L"",
            kTweakBase + static_cast<int>(i),
            card_x + card_w - pad - toggle_w,
            inner - dp(2),
            toggle_w,
            toggle_h,
            ButtonKind::Toggle);
        state.buttons[toggle].checked = package.enabled;
        state.tweak_toggles.push_back(toggle);
        inner += dp(26);

        if (!package.description.empty()) {
            const int width = card_w - pad * 2 - toggle_w - dp(12);
            const int height = measure_text_height(page, app.font_small, package.description, width);
            make_label(page, package.description, app.font_small, card_x + pad, inner, width, height, true, true);
            inner += height + dp(6);
        }

        if (!package.settings.empty()) {
            inner += dp(8);
            for (size_t setting_index = 0; setting_index < package.settings.size(); ++setting_index) {
                const auto& setting = package.settings[setting_index];
                const std::wstring value = setting_value(state.draft_config, setting);
                const int row_h = dp(34);
                const int id = kSettingBase + static_cast<int>(state.setting_controls.size());

                HWND control = nullptr;
                if (setting.type == L"bool") {
                    make_label(page, setting.label, app.font_ui, card_x + pad + dp(12), inner + dp(7), card_w - pad * 2 - dp(12) - toggle_w - dp(12), dp(20), true);
                    control = make_button(
                        state, page, L"", id,
                        card_x + card_w - pad - toggle_w, inner + dp(4), toggle_w, toggle_h, ButtonKind::Toggle);
                    state.buttons[control].checked = parse_bool(value, parse_bool(setting.default_value, false));
                } else {
                    const int edit_w = dp(150);
                    make_label(page, setting.label, app.font_ui, card_x + pad + dp(12), inner + dp(7), card_w - pad * 2 - dp(12) - edit_w - dp(12), dp(20), true);
                    control = make_edit(
                        page, value, app.font_ui, id,
                        card_x + card_w - pad - edit_w + dp(1), inner + dp(3), edit_w - dp(2), dp(26),
                        setting.type == L"int");
                    info.fields.push_back(RECT{card_x + card_w - pad - edit_w, inner + dp(2), card_x + card_w - pad, inner + dp(30)});
                }

                state.setting_controls.push_back(SettingControl{i, setting_index, control});
                inner += row_h;
            }
            inner -= dp(4);
        }

        inner += pad;
        info.cards.push_back(RECT{card_x, card_top, card_x + card_w, inner});
        y = inner + dp(16);
    }

    info.content_height = y + dp(4);
}

void build_about_page(SettingsState& state) {
    HWND page = state.pages[2];
    PageState& info = state.page_state[2];
    GuiState& app = *state.app;

    const int card_x = dp(kCardMargin);
    const int card_w = dp(kWinW - kSidebarW - kCardMargin * 2);
    const int pad = dp(20);
    int y = dp(4);

    {
        const int card_top = y;
        int inner = y + pad;

        make_label(page, kBrand, app.font_section, card_x + pad, inner, card_w - pad * 2, dp(22), false);
        inner += dp(28);

        const std::wstring blurb =
            L"A launcher for EMERGENCY 4 that loads optional fixes and tweaks before the game starts. "
            L"Tweaks are independent hook packages the launcher discovers at runtime, so new ones can be "
            L"dropped in without updating the launcher.";
        const int blurb_h = measure_text_height(page, app.font_small, blurb, card_w - pad * 2);
        make_label(page, blurb, app.font_small, card_x + pad, inner, card_w - pad * 2, blurb_h, true, true);
        inner += blurb_h + dp(16);

        make_label(
            page,
            std::to_wstring(state.draft_packages.size()) + L" tweak package(s) installed",
            app.font_ui,
            card_x + pad,
            inner,
            card_w - pad * 2,
            dp(20),
            true);
        inner += dp(28) + pad - dp(8);

        info.cards.push_back(RECT{card_x, card_top, card_x + card_w, inner});
        y = inner + dp(20);
    }

    {
        const int card_top = y;
        int inner = y + pad;

        make_label(page, L"Shortcuts", app.font_section, card_x + pad, inner, card_w - pad * 2, dp(20), false);
        inner += dp(30);

        make_button(state, page, L"Open logs folder", kIdOpenLogs, card_x + pad, inner, dp(150), dp(34), ButtonKind::Secondary);
        make_button(state, page, L"Open hooks folder", kIdOpenHooks, card_x + pad + dp(160), inner, dp(158), dp(34), ButtonKind::Secondary);
        make_button(state, page, L"Open config.ini", kIdOpenConfig, card_x + pad + dp(328), inner, dp(146), dp(34), ButtonKind::Ghost);
        inner += dp(34) + pad;

        info.cards.push_back(RECT{card_x, card_top, card_x + card_w, inner});
        y = inner + dp(20);
    }

    info.content_height = y;
}

void select_page(SettingsState& state, int index) {
    state.active_page = index;
    for (int i = 0; i < 3; ++i) {
        ShowWindow(state.pages[i], i == index ? SW_SHOW : SW_HIDE);
        state.buttons[state.nav[i]].checked = i == index;
        InvalidateRect(state.nav[i], nullptr, TRUE);
    }
    InvalidateRect(state.window, nullptr, TRUE);
}

void build_settings_layout(SettingsState& state) {
    GuiState& app = *state.app;
    HWND window = state.window;

    for (int i = 0; i < 3; ++i) {
        state.page_state[i].owner = &state;
        state.page_state[i].index = i;
        state.pages[i] = CreateWindowExW(
            WS_EX_CONTROLPARENT,
            L"OMoutaEM4TweaksPage",
            L"",
            WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL,
            dp(kSidebarW),
            dp(kPageTop),
            dp(kWinW - kSidebarW),
            page_height(),
            window,
            nullptr,
            GetModuleHandleW(nullptr),
            &state.page_state[i]);
    }

    build_general_page(state);
    build_tweaks_page(state);
    build_about_page(state);

    for (int i = 0; i < 3; ++i) {
        update_scroll_info(state.page_state[i], state.pages[i]);
        EnableScrollBar(state.pages[i], SB_VERT, state.page_state[i].content_height > page_height() ? ESB_ENABLE_BOTH : ESB_DISABLE_BOTH);
    }

    const int nav_y = dp(150);
    for (int i = 0; i < 3; ++i) {
        state.nav[i] = make_button(
            state, window, kNavLabels[i], kIdNavBase + i,
            dp(14), nav_y + dp(52) * i, dp(188), dp(44), ButtonKind::Nav);
    }

    state.sidebar_note = make_label(
        window,
        L"",
        app.font_small,
        dp(24),
        dp(kWinH - kActionBarH) - dp(44),
        dp(kSidebarW - 40),
        dp(18),
        true);

    const int action_y = dp(kWinH - kActionBarH) + (dp(kActionBarH) - dp(40)) / 2;
    const int right = dp(kWinW) - dp(24);
    make_button(state, window, L"Play game", kIdSaveLaunch, right - dp(132), action_y, dp(132), dp(40), ButtonKind::Primary);
    make_button(state, window, L"Save & close", kIdSaveClose, right - dp(132) - dp(10) - dp(126), action_y, dp(126), dp(40), ButtonKind::Secondary);
    make_button(state, window, L"Cancel", kIdCancel, right - dp(132) - dp(10) - dp(126) - dp(10) - dp(90), action_y, dp(90), dp(40), ButtonKind::Ghost);
}

void paint_settings_chrome(SettingsState& state, HDC dc) {
    RECT client{};
    GetClientRect(state.window, &client);

    HBRUSH background = CreateSolidBrush(kColBg);
    FillRect(dc, &client, background);
    DeleteObject(background);

    HBRUSH sidebar = CreateSolidBrush(kColSidebar);
    RECT sidebar_rect{0, 0, dp(kSidebarW), client.bottom};
    FillRect(dc, &sidebar_rect, sidebar);
    RECT action_rect{dp(kSidebarW), dp(kWinH - kActionBarH), client.right, client.bottom};
    FillRect(dc, &action_rect, sidebar);
    DeleteObject(sidebar);

    HBRUSH border = CreateSolidBrush(kColBorderSoft);
    RECT sidebar_edge{dp(kSidebarW) - 1, 0, dp(kSidebarW), client.bottom};
    FillRect(dc, &sidebar_edge, border);
    RECT action_edge{dp(kSidebarW), dp(kWinH - kActionBarH), client.right, dp(kWinH - kActionBarH) + 1};
    FillRect(dc, &action_edge, border);
    DeleteObject(border);

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    draw_logo(graphics, state.app->logo.get(), static_cast<float>(dp(26)), static_cast<float>(dp(28)), static_cast<float>(dp(164)));

    SetBkMode(dc, TRANSPARENT);

    HGDIOBJ old_font = SelectObject(dc, state.app->font_title);
    SetTextColor(dc, kColText);
    RECT title{dp(kSidebarW + kCardMargin), dp(30), client.right - dp(24), dp(62)};
    DrawTextW(dc, kPageTitles[state.active_page], -1, &title, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    SelectObject(dc, state.app->font_small);
    SetTextColor(dc, kColMuted);
    RECT subtitle{dp(kSidebarW + kCardMargin), dp(64), client.right - dp(24), dp(86)};
    DrawTextW(dc, kPageSubtitles[state.active_page], -1, &subtitle, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

// --- actions ---------------------------------------------------------------

void browse_for_exe(SettingsState& state, HWND edit, const wchar_t* title, const wchar_t* filter) {
    std::array<wchar_t, MAX_PATH> path{};
    wcsncpy_s(path.data(), path.size(), window_text(edit).c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = state.window;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path.data();
    ofn.nMaxFile = static_cast<DWORD>(path.size());
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(edit, path.data());
    }
}

void read_draft(SettingsState& state) {
    state.draft_config.em4_path = trim(window_text(state.em4_edit));

    for (size_t i = 0; i < state.draft_packages.size() && i < state.tweak_toggles.size(); ++i) {
        state.draft_packages[i].enabled = state.buttons[state.tweak_toggles[i]].checked;
    }

    for (const auto& binding : state.setting_controls) {
        const auto& setting = state.draft_packages[binding.package_index].settings[binding.setting_index];
        if (setting.type == L"bool") {
            state.draft_config.sections[setting.section][setting.key] =
                state.buttons[binding.control].checked ? L"1" : L"0";
        } else {
            state.draft_config.sections[setting.section][setting.key] = trim(window_text(binding.control));
        }
    }

    sync_config_from_packages(state.draft_packages, state.draft_config);
}

void finish_settings(SettingsState& state, SettingsResult result, bool save) {
    if (save) {
        read_draft(state);
        *state.app->target_config = state.draft_config;
        *state.app->target_packages = state.draft_packages;
        write_config(state.app->config_path, *state.app->target_config);
    }
    state.result = result;
    DestroyWindow(state.window);
}

void update_sidebar_note(SettingsState& state) {
    size_t on = 0;
    for (size_t i = 0; i < state.draft_packages.size() && i < state.tweak_toggles.size(); ++i) {
        if (state.buttons[state.tweak_toggles[i]].checked) {
            ++on;
        }
    }
    SetWindowTextW(
        state.sidebar_note,
        (std::to_wstring(on) + L" of " + std::to_wstring(state.draft_packages.size()) + L" tweaks enabled").c_str());
}

LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE:
        state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        state->window = window;
        build_settings_layout(*state);
        select_page(*state, 0);
        update_large_address_status(*state);
        update_sidebar_note(*state);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (state) {
            paint_settings_chrome(*state, dc);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        if (!state) {
            break;
        }
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetWindowLongPtrW(reinterpret_cast<HWND>(lparam), GWLP_USERDATA) == 1 ? kColMuted : kColText);
        // The only labels parented to this window live in the sidebar.
        return reinterpret_cast<LRESULT>(state->brush_sidebar);
    }
    case WM_DRAWITEM:
        if (state) {
            draw_owner_button(*state, reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
            return TRUE;
        }
        break;
    case WM_COMMAND: {
        if (!state) {
            break;
        }
        const int id = LOWORD(wparam);
        const int code = HIWORD(wparam);

        if (id == kIdEm4Edit && code == EN_CHANGE) {
            update_large_address_status(*state);
            return 0;
        }

        if (id >= kIdNavBase && id < kIdNavBase + 3) {
            select_page(*state, id - kIdNavBase);
            return 0;
        }

        if (id >= kTweakBase && id < kTweakBase + 1000) {
            HWND toggle = reinterpret_cast<HWND>(lparam);
            auto entry = state->buttons.find(toggle);
            if (entry != state->buttons.end()) {
                entry->second.checked = !entry->second.checked;
                InvalidateRect(toggle, nullptr, TRUE);
                update_sidebar_note(*state);
            }
            return 0;
        }

        if (id >= kSettingBase && id < kSettingBase + 1000) {
            HWND toggle = reinterpret_cast<HWND>(lparam);
            auto entry = state->buttons.find(toggle);
            if (entry != state->buttons.end() && entry->second.kind == ButtonKind::Toggle) {
                entry->second.checked = !entry->second.checked;
                InvalidateRect(toggle, nullptr, TRUE);
            }
            return 0;
        }

        switch (id) {
        case kIdBrowseEm4:
            browse_for_exe(*state, state->em4_edit, L"Select the EMERGENCY 4 executable",
                           L"EM4 executable\0em4.exe;Em4.exe\0Executable files\0*.exe\0All files\0*.*\0");
            return 0;
        case kIdPatch4Gb: {
            std::wstring error;
            if (!patch_large_address_aware(current_em4_path(*state), error)) {
                MessageBoxW(window, error.c_str(), kBrand, MB_OK | MB_ICONERROR);
            }
            update_large_address_status(*state);
            return 0;
        }
        case kIdRestore4Gb: {
            std::wstring error;
            if (!restore_large_address_backup(current_em4_path(*state), error)) {
                MessageBoxW(window, error.c_str(), kBrand, MB_OK | MB_ICONERROR);
            }
            update_large_address_status(*state);
            return 0;
        }
        case kIdOpenLogs:
            ShellExecuteW(window, L"open", (state->app->root / kDataDir / kLogsDir).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case kIdOpenHooks:
            ShellExecuteW(window, L"open", hooks_dir(state->app->root).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case kIdOpenConfig:
            ShellExecuteW(window, L"open", state->app->config_path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case kIdSaveLaunch:
            finish_settings(*state, SettingsResult::Launch, true);
            return 0;
        case kIdSaveClose:
            finish_settings(*state, SettingsResult::Close, true);
            return 0;
        case kIdCancel:
        case IDCANCEL:
            finish_settings(*state, SettingsResult::Cancel, false);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        if (state) {
            finish_settings(*state, SettingsResult::Cancel, false);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

SettingsResult run_settings(GuiState& app) {
    SettingsState state;
    state.app = &app;
    state.draft_config = *app.target_config;
    state.draft_packages = *app.target_packages;
    state.brush_bg = CreateSolidBrush(kColBg);
    state.brush_card = CreateSolidBrush(kColCard);
    state.brush_field = CreateSolidBrush(kColField);
    state.brush_sidebar = CreateSolidBrush(kColSidebar);

    RECT frame{0, 0, dp(kWinW), dp(kWinH)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectEx(&frame, style, FALSE, 0);

    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    const int x = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - width) / 2);
    const int y = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - height) / 2);

    HWND window = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        L"OMoutaEM4TweaksSettings",
        kBrand,
        style,
        x,
        y,
        width,
        height,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);

    apply_dark_titlebar(window);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    DeleteObject(state.brush_bg);
    DeleteObject(state.brush_card);
    DeleteObject(state.brush_field);
    DeleteObject(state.brush_sidebar);
    return state.result;
}

SplashAction run_splash(GuiState& app) {
    SplashState state;
    state.app = &app;

    const int width = dp(kSplashCardW + kSplashMargin * 2);
    const int height = dp(kSplashCardH + kSplashMargin * 2);
    const int x = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - width) / 2);
    const int y = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - height) / 2);

    HWND window = CreateWindowExW(
        WS_EX_LAYERED,
        L"OMoutaEM4TweaksSplash",
        kBrand,
        WS_POPUP,
        x,
        y,
        width,
        height,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);

    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return state.action;
}

void register_window_classes(HINSTANCE instance) {
    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_OMouta_EM4_TWEAKS));
    HCURSOR cursor = LoadCursorW(nullptr, IDC_ARROW);

    WNDCLASSW splash{};
    splash.lpfnWndProc = splash_proc;
    splash.hInstance = instance;
    splash.hIcon = icon;
    splash.hCursor = cursor;
    splash.lpszClassName = L"OMoutaEM4TweaksSplash";
    RegisterClassW(&splash);

    WNDCLASSW settings{};
    settings.lpfnWndProc = settings_proc;
    settings.hInstance = instance;
    settings.hIcon = icon;
    settings.hCursor = cursor;
    settings.lpszClassName = L"OMoutaEM4TweaksSettings";
    RegisterClassW(&settings);

    WNDCLASSW page{};
    page.lpfnWndProc = page_proc;
    page.hInstance = instance;
    page.hCursor = cursor;
    page.lpszClassName = L"OMoutaEM4TweaksPage";
    RegisterClassW(&page);
}

} // namespace

GuiResult show_launcher_window(
    Config& config,
    std::vector<TweakPackage>& packages,
    const std::filesystem::path& config_path) {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gdiplus_input{};
    ULONG_PTR gdiplus_token = 0;
    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);

    {
        HDC screen = GetDC(nullptr);
        g_dpi = std::max(96, GetDeviceCaps(screen, LOGPIXELSX));
        ReleaseDC(nullptr, screen);
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    register_window_classes(instance);

    GuiState app;
    app.target_config = &config;
    app.target_packages = &packages;
    app.config_path = config_path;
    app.root = module_dir();
    app.logo = load_png_resource(instance, IDB_OMouta_TWEAKS_LOGO);
    app.font_ui = make_font(9);
    app.font_small = make_font(8);
    app.font_section = make_font(11, FW_SEMIBOLD);
    app.font_title = make_font(17, FW_SEMIBOLD);
    app.font_nav = make_font(10, FW_MEDIUM);

    GuiResult result = GuiResult::Cancel;
    for (;;) {
        const SplashAction action = run_splash(app);
        if (action == SplashAction::Play) {
            result = GuiResult::PlayGame;
            break;
        }
        if (action == SplashAction::Editor) {
            result = GuiResult::OpenEditor;
            break;
        }
        if (action != SplashAction::Settings) {
            result = GuiResult::Cancel;
            break;
        }

        if (run_settings(app) == SettingsResult::Launch) {
            result = GuiResult::PlayGame;
            break;
        }
        // Otherwise fall through and show the splash again with the new settings.
    }

    app.logo.reset();
    DeleteObject(app.font_ui);
    DeleteObject(app.font_small);
    DeleteObject(app.font_section);
    DeleteObject(app.font_title);
    DeleteObject(app.font_nav);

    if (gdiplus_token) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
    }

    return result;
}

} // namespace om4t::launcher
