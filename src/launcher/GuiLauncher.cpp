#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "GuiLauncher.h"
#include "PePatch.h"

#include "../shared/AppPaths.h"
#include "../shared/StringUtil.h"
#include "../../resources/windows/resource.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace om4t::launcher {

namespace {

constexpr int kTimerId = 1001;
constexpr int kCountdownSeconds = 5;
constexpr int kSettingsButton = 2001;
constexpr int kBrowseButton = 2002;
constexpr int kSaveLaunchButton = 2003;
constexpr int kSaveExitButton = 2004;
constexpr int kExitButton = 2005;
constexpr int kPatch4GbButton = 2006;
constexpr int kRestore4GbButton = 2007;
constexpr int kEm4Path = 2100;
constexpr int kTab = 2200;
constexpr int kTweakBase = 3000;
constexpr int kSplashWidth = 360;
constexpr int kSplashHeight = 450;
constexpr int kSettingsWidth = 760;
constexpr int kSettingsHeight = 560;
constexpr int kSettingsContentTop = 126;
constexpr int kSettingsContentBottom = 460;
constexpr int kScrollLine = 30;
constexpr int kScrollPage = 240;
constexpr COLORREF kWindowBg = RGB(246, 247, 249);
constexpr COLORREF kHeaderBg = RGB(255, 255, 255);
constexpr COLORREF kTextColor = RGB(32, 35, 39);
constexpr COLORREF kMutedTextColor = RGB(96, 103, 112);

struct SettingControl {
    size_t package_index = 0;
    size_t setting_index = 0;
    HWND control = nullptr;
};

struct GuiState {
    Config* target_config = nullptr;
    std::vector<TweakPackage>* target_packages = nullptr;
    Config draft_config;
    std::vector<TweakPackage> draft_packages;
    std::filesystem::path config_path;
    GuiResult result = GuiResult::Cancel;
    bool finished = false;
    bool settings_open = false;
    bool settings_laid_out = false;
    int remaining = kCountdownSeconds;
    HWND window = nullptr;
    std::unique_ptr<Gdiplus::Bitmap> splash_logo;
    RECT splash_settings_rect{116, 382, 244, 420};
    HFONT ui_font = nullptr;
    HFONT title_font = nullptr;
    HFONT section_font = nullptr;
    HBRUSH window_brush = nullptr;
    HBRUSH header_brush = nullptr;
    HWND tab = nullptr;
    HWND em4_path = nullptr;
    HWND large_address_status = nullptr;
    HWND patch_4gb = nullptr;
    HWND restore_4gb = nullptr;
    int active_tab = 0;
    int tweak_scroll = 0;
    int tweak_scroll_max = 0;
    int tweak_content_height = 0;
    std::vector<HWND> general_controls;
    std::vector<HWND> tweak_controls;
    std::vector<HWND> about_controls;
    std::vector<HWND> action_controls;
    std::vector<HWND> header_controls;
    std::vector<HWND> tweak_checks;
    std::vector<SettingControl> setting_controls;
    std::map<HWND, RECT> tweak_base_rects;
};

std::wstring window_text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length), L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    return text;
}

void set_font(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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

HWND make_control_ex(HWND parent, DWORD ex_style, const wchar_t* klass, const std::wstring& text, DWORD style, int id, int x, int y, int w, int h, HFONT font) {
    HWND control = CreateWindowExW(
        ex_style,
        klass,
        text.c_str(),
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
    set_font(control, font ? font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    return control;
}

HWND make_control(HWND parent, const wchar_t* klass, const std::wstring& text, DWORD style, int id, int x, int y, int w, int h, HFONT font = nullptr) {
    return make_control_ex(parent, 0, klass, text, style, id, x, y, w, h, font);
}

HWND make_edit(HWND parent, const std::wstring& text, int id, int x, int y, int w, int h) {
    return make_control_ex(parent, WS_EX_CLIENTEDGE, L"EDIT", text, ES_AUTOHSCROLL, id, x, y, w, h, nullptr);
}

void set_visible(const std::vector<HWND>& controls, bool visible) {
    for (HWND control : controls) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    }
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

std::wstring setting_value(const Config& config, const TweakSetting& setting) {
    if (auto section = config.sections.find(setting.section); section != config.sections.end()) {
        if (auto value = section->second.find(setting.key); value != section->second.end()) {
            return value->second;
        }
    }
    return setting.default_value;
}

void layout_settings(HWND window, GuiState& state);
void update_large_address_status(GuiState& state);

void update_splash_status(GuiState& state) {
    InvalidateRect(state.window, nullptr, FALSE);
}

void render_splash(GuiState& state) {
    if (!state.window || state.settings_open) {
        return;
    }

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = kSplashWidth;
    info.bmiHeader.biHeight = -kSplashHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old_bitmap = SelectObject(memory, bitmap);

    Gdiplus::Graphics graphics(memory);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    if (state.splash_logo) {
        graphics.DrawImage(state.splash_logo.get(), Gdiplus::Rect(40, 24, 280, 280));
    }

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font status_font(&family, 15.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font button_font(&family, 16.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat center;
    center.SetAlignment(Gdiplus::StringAlignmentCenter);
    center.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    const std::wstring status = L"Launching in " + std::to_wstring(state.remaining) + L" seconds";
    Gdiplus::SolidBrush text_brush(Gdiplus::Color(235, 255, 255, 255));
    Gdiplus::RectF status_rect(40.0f, 318.0f, 280.0f, 28.0f);
    graphics.DrawString(status.c_str(), -1, &status_font, status_rect, &center, &text_brush);

    Gdiplus::GraphicsPath button_path;
    const auto& r = state.splash_settings_rect;
    const int radius = 8;
    button_path.AddArc(r.left, r.top, radius * 2, radius * 2, 180, 90);
    button_path.AddArc(r.right - radius * 2, r.top, radius * 2, radius * 2, 270, 90);
    button_path.AddArc(r.right - radius * 2, r.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
    button_path.AddArc(r.left, r.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
    button_path.CloseFigure();

    Gdiplus::SolidBrush button_brush(Gdiplus::Color(230, 24, 24, 28));
    Gdiplus::Pen button_pen(Gdiplus::Color(180, 255, 255, 255), 1.0f);
    graphics.FillPath(&button_brush, &button_path);
    graphics.DrawPath(&button_pen, &button_path);
    Gdiplus::RectF button_rect(static_cast<Gdiplus::REAL>(r.left), static_cast<Gdiplus::REAL>(r.top), static_cast<Gdiplus::REAL>(r.right - r.left), static_cast<Gdiplus::REAL>(r.bottom - r.top));
    graphics.DrawString(L"Settings", -1, &button_font, button_rect, &center, &text_brush);

    POINT source{0, 0};
    SIZE size{kSplashWidth, kSplashHeight};
    POINT destination{};
    RECT window_rect{};
    GetWindowRect(state.window, &window_rect);
    destination.x = window_rect.left;
    destination.y = window_rect.top;
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(state.window, screen, &destination, &size, memory, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

void select_tab(GuiState& state, int index) {
    state.active_tab = index;
    set_visible(state.general_controls, index == 0);
    set_visible(state.tweak_controls, false);
    set_visible(state.about_controls, index == 2);
    if (index == 1) {
        SetWindowLongPtrW(state.window, GWL_STYLE, GetWindowLongPtrW(state.window, GWL_STYLE) | WS_VSCROLL);
        SetWindowPos(state.window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        SCROLLINFO scroll{};
        scroll.cbSize = sizeof(scroll);
        scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        scroll.nMin = 0;
        scroll.nMax = state.tweak_content_height;
        scroll.nPage = kSettingsContentBottom - kSettingsContentTop;
        scroll.nPos = state.tweak_scroll;
        SetScrollInfo(state.window, SB_VERT, &scroll, TRUE);
        ShowScrollBar(state.window, SB_VERT, state.tweak_scroll_max > 0);

        for (HWND control : state.tweak_controls) {
            if (auto it = state.tweak_base_rects.find(control); it != state.tweak_base_rects.end()) {
                const RECT& base = it->second;
                const int y = base.top - state.tweak_scroll;
                const int h = base.bottom - base.top;
                const bool visible = y + h > kSettingsContentTop && y < kSettingsContentBottom;
                SetWindowPos(control, nullptr, base.left, y, base.right - base.left, h, SWP_NOZORDER | SWP_NOACTIVATE);
                ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
            }
        }
    } else {
        ShowScrollBar(state.window, SB_VERT, FALSE);
        SetWindowLongPtrW(state.window, GWL_STYLE, GetWindowLongPtrW(state.window, GWL_STYLE) & ~WS_VSCROLL);
        SetWindowPos(state.window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

void show_settings(GuiState& state) {
    state.settings_open = true;
    KillTimer(state.window, kTimerId);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_OMouta_EM4_TWEAKS));
    SetWindowTextW(state.window, kBrand);
    SendMessageW(state.window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    SendMessageW(state.window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));

    LONG_PTR ex_style = GetWindowLongPtrW(state.window, GWL_EXSTYLE);
    SetWindowLongPtrW(state.window, GWL_EXSTYLE, (ex_style & ~WS_EX_LAYERED) | WS_EX_APPWINDOW);
    SetWindowLongPtrW(state.window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetWindowPos(state.window, nullptr, 0, 0, kSettingsWidth, kSettingsHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);

    if (!state.settings_laid_out) {
        layout_settings(state.window, state);
        state.settings_laid_out = true;
    }

    set_visible(state.header_controls, true);
    ShowWindow(state.tab, SW_SHOW);
    set_visible(state.action_controls, true);
    TabCtrl_SetCurSel(state.tab, 0);
    select_tab(state, 0);
    update_large_address_status(state);
    RedrawWindow(state.window, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

std::filesystem::path current_em4_path(const GuiState& state) {
    std::filesystem::path path = trim(window_text(state.em4_path));
    if (path.is_relative()) {
        path = module_dir() / path;
    }
    return std::filesystem::absolute(path);
}

void update_large_address_status(GuiState& state) {
    if (!state.large_address_status) {
        return;
    }

    const auto info = inspect_large_address_aware(current_em4_path(state));
    std::wstring status = L"Memory headroom: " + info.message;
    if (info.backup_exists) {
        status += L" Backup available.";
    }

    SetWindowTextW(state.large_address_status, status.c_str());
    EnableWindow(state.patch_4gb, info.state == LargeAddressAwareState::Disabled);
    EnableWindow(state.restore_4gb, info.backup_exists);
}

void patch_4gb(GuiState& state) {
    std::wstring error;
    if (!patch_large_address_aware(current_em4_path(state), error)) {
        MessageBoxW(state.window, error.c_str(), kBrand, MB_OK | MB_ICONERROR);
    }
    update_large_address_status(state);
}

void restore_4gb(GuiState& state) {
    std::wstring error;
    if (!restore_large_address_backup(current_em4_path(state), error)) {
        MessageBoxW(state.window, error.c_str(), kBrand, MB_OK | MB_ICONERROR);
    }
    update_large_address_status(state);
}

void browse_for_em4(GuiState& state) {
    std::array<wchar_t, MAX_PATH> path{};
    wcsncpy_s(path.data(), path.size(), window_text(state.em4_path).c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = state.window;
    ofn.lpstrFilter = L"EM4 executable\0em4.exe;Em4.exe\0Executable files\0*.exe\0All files\0*.*\0";
    ofn.lpstrFile = path.data();
    ofn.nMaxFile = static_cast<DWORD>(path.size());
    ofn.lpstrTitle = L"Select EM4 executable";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(state.em4_path, path.data());
        update_large_address_status(state);
    }
}

void read_draft_controls(GuiState& state) {
    state.draft_config.em4_path = trim(window_text(state.em4_path));

    for (size_t i = 0; i < state.draft_packages.size(); ++i) {
        state.draft_packages[i].enabled = SendMessageW(state.tweak_checks[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    for (const auto& binding : state.setting_controls) {
        const auto& setting = state.draft_packages[binding.package_index].settings[binding.setting_index];
        if (setting.type == L"bool") {
            state.draft_config.sections[setting.section][setting.key] =
                SendMessageW(binding.control, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0";
        } else {
            state.draft_config.sections[setting.section][setting.key] = trim(window_text(binding.control));
        }
    }

    sync_config_from_packages(state.draft_packages, state.draft_config);
}

void commit_and_finish(GuiState& state, GuiResult result) {
    read_draft_controls(state);
    *state.target_config = state.draft_config;
    *state.target_packages = state.draft_packages;
    write_config(state.config_path, *state.target_config);
    state.result = result;
    state.finished = true;
    DestroyWindow(state.window);
}

void finish_without_saving(GuiState& state, GuiResult result) {
    state.result = result;
    state.finished = true;
    DestroyWindow(state.window);
}

void add_tab(HWND tab, const wchar_t* text, int index) {
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(text);
    TabCtrl_InsertItem(tab, index, &item);
}

void layout_splash(HWND window, GuiState& state) {
    render_splash(state);
}

void add_general_controls(HWND window, GuiState& state) {
    state.general_controls.push_back(make_control(window, L"STATIC", L"Game executable", SS_LEFT, 0, 34, 138, 180, 20, state.section_font));
    state.general_controls.push_back(make_control(window, L"STATIC", L"Choose the EM4 executable the launcher should start.", SS_LEFT, 0, 34, 164, 540, 20, state.ui_font));
    state.em4_path = make_edit(window, state.draft_config.em4_path.wstring(), kEm4Path, 34, 198, 560, 26);
    state.general_controls.push_back(state.em4_path);
    state.general_controls.push_back(make_control(window, L"BUTTON", L"Browse", BS_PUSHBUTTON | BS_FLAT, kBrowseButton, 608, 197, 100, 28, state.ui_font));

    state.general_controls.push_back(make_control(window, L"STATIC", L"4 GB Patch Helper", SS_LEFT, 0, 34, 256, 220, 20, state.section_font));
    state.general_controls.push_back(make_control(window, L"STATIC", L"Large Address Aware lets 32-bit EM4 use more memory on modern Windows.", SS_LEFT, 0, 34, 282, 620, 20, state.ui_font));
    state.large_address_status = make_control(window, L"STATIC", L"", SS_LEFT, 0, 34, 310, 620, 22, state.ui_font);
    state.general_controls.push_back(state.large_address_status);
    state.patch_4gb = make_control(window, L"BUTTON", L"Apply 4 GB Patch", BS_PUSHBUTTON | BS_FLAT, kPatch4GbButton, 34, 342, 138, 30, state.ui_font);
    state.restore_4gb = make_control(window, L"BUTTON", L"Restore Backup", BS_PUSHBUTTON | BS_FLAT, kRestore4GbButton, 184, 342, 126, 30, state.ui_font);
    state.general_controls.push_back(state.patch_4gb);
    state.general_controls.push_back(state.restore_4gb);
}

void add_tweak_controls(HWND window, GuiState& state) {
    int y = kSettingsContentTop;
    if (state.draft_packages.empty()) {
        state.tweak_controls.push_back(make_control(window, L"STATIC", L"No tweak packages were found.", SS_LEFT, 0, 34, y, 520, 22, state.ui_font));
        state.tweak_content_height = y + 34;
        return;
    }

    for (size_t i = 0; i < state.draft_packages.size(); ++i) {
        auto& package = state.draft_packages[i];
        HWND check = make_control(window, L"BUTTON", package.name, BS_AUTOCHECKBOX, kTweakBase + static_cast<int>(i), 34, y, 420, 22, state.section_font);
        SendMessageW(check, BM_SETCHECK, package.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        state.tweak_checks.push_back(check);
        state.tweak_controls.push_back(check);

        if (!package.description.empty()) {
            state.tweak_controls.push_back(make_control(window, L"STATIC", package.description, SS_LEFT, 0, 54, y + 26, 640, 20, state.ui_font));
            y += 48;
        } else {
            y += 28;
        }

        for (size_t setting_index = 0; setting_index < package.settings.size(); ++setting_index) {
            const auto& setting = package.settings[setting_index];
            const std::wstring value = setting_value(state.draft_config, setting);
            state.tweak_controls.push_back(make_control(window, L"STATIC", setting.label, SS_LEFT, 0, 72, y + 4, 300, 20, state.ui_font));
            HWND control = nullptr;
            if (setting.type == L"bool") {
                control = make_control(window, L"BUTTON", L"", BS_AUTOCHECKBOX, 0, 392, y, 22, 22, state.ui_font);
                SendMessageW(control, BM_SETCHECK, parse_bool(value, parse_bool(setting.default_value, false)) ? BST_CHECKED : BST_UNCHECKED, 0);
            } else {
                DWORD style = ES_AUTOHSCROLL;
                if (setting.type == L"int") {
                    style |= ES_NUMBER;
                }
                control = make_control_ex(window, WS_EX_CLIENTEDGE, L"EDIT", value, style, 0, 392, y, 140, 24, state.ui_font);
            }
            state.tweak_controls.push_back(control);
            state.setting_controls.push_back(SettingControl{i, setting_index, control});
            y += 30;
        }

        y += 12;
    }

    state.tweak_content_height = y;
    state.tweak_scroll_max = std::max(0, state.tweak_content_height - kSettingsContentBottom + 12);
}

void add_about_controls(HWND window, GuiState& state) {
    state.about_controls.push_back(make_control(window, L"STATIC", kBrand, SS_LEFT, 0, 34, 138, 520, 28, state.title_font));
    state.about_controls.push_back(make_control(window, L"STATIC", L"Native launcher for EMERGENCY 4 tweak packages.", SS_LEFT, 0, 34, 178, 600, 22, state.ui_font));
}

void add_header_controls(HWND window, GuiState& state) {
    HWND icon = make_control(window, L"STATIC", L"", SS_ICON | SS_CENTERIMAGE, 0, 28, 20, 48, 48, state.ui_font);
    HICON app_icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_OMouta_EM4_TWEAKS));
    SendMessageW(icon, STM_SETICON, reinterpret_cast<WPARAM>(app_icon), 0);
    state.header_controls.push_back(icon);
    state.header_controls.push_back(make_control(window, L"STATIC", kBrand, SS_LEFT, 0, 88, 18, 520, 30, state.title_font));
    state.header_controls.push_back(make_control(window, L"STATIC", L"Launch EM4 with selected compatibility tweaks.", SS_LEFT, 0, 90, 52, 560, 20, state.ui_font));
}

void layout_settings(HWND window, GuiState& state) {
    if (!state.ui_font) {
        state.ui_font = make_font(9);
        state.title_font = make_font(18, FW_SEMIBOLD);
        state.section_font = make_font(10, FW_SEMIBOLD);
        state.window_brush = CreateSolidBrush(kWindowBg);
        state.header_brush = CreateSolidBrush(kHeaderBg);
    }

    add_header_controls(window, state);

    state.tab = make_control(window, WC_TABCONTROLW, L"", WS_CLIPSIBLINGS, kTab, 24, 90, 712, 370, state.ui_font);
    add_tab(state.tab, L"General", 0);
    add_tab(state.tab, L"Tweaks", 1);
    add_tab(state.tab, L"About", 2);

    add_general_controls(window, state);
    add_tweak_controls(window, state);
    add_about_controls(window, state);

    for (HWND control : state.tweak_controls) {
        RECT rect{};
        GetWindowRect(control, &rect);
        POINT top_left{rect.left, rect.top};
        POINT bottom_right{rect.right, rect.bottom};
        ScreenToClient(window, &top_left);
        ScreenToClient(window, &bottom_right);
        state.tweak_base_rects[control] = RECT{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    }

    state.action_controls.push_back(make_control(window, L"BUTTON", L"Save && Launch", BS_DEFPUSHBUTTON | BS_FLAT, kSaveLaunchButton, 410, 492, 124, 32, state.ui_font));
    state.action_controls.push_back(make_control(window, L"BUTTON", L"Save && Exit", BS_PUSHBUTTON | BS_FLAT, kSaveExitButton, 542, 492, 106, 32, state.ui_font));
    state.action_controls.push_back(make_control(window, L"BUTTON", L"Exit", BS_PUSHBUTTON | BS_FLAT, kExitButton, 656, 492, 64, 32, state.ui_font));

    ShowWindow(state.tab, SW_HIDE);
    set_visible(state.header_controls, false);
    set_visible(state.general_controls, false);
    set_visible(state.tweak_controls, false);
    set_visible(state.about_controls, false);
    set_visible(state.action_controls, false);
}

void set_tweak_scroll(GuiState& state, int position) {
    state.tweak_scroll = std::clamp(position, 0, state.tweak_scroll_max);
    if (state.active_tab == 1) {
        select_tab(state, 1);
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<GuiState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE:
        state = reinterpret_cast<GuiState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        state->window = window;
        layout_splash(window, *state);
        SetTimer(window, kTimerId, 1000, nullptr);
        return 0;
    case WM_ERASEBKGND:
        if (state && state->settings_open) {
            RECT rect{};
            GetClientRect(window, &rect);
            FillRect(reinterpret_cast<HDC>(wparam), &rect, state->window_brush);

            RECT header{0, 0, rect.right, 86};
            FillRect(reinterpret_cast<HDC>(wparam), &header, state->header_brush);

            HGDIOBJ old_pen = SelectObject(reinterpret_cast<HDC>(wparam), CreatePen(PS_SOLID, 1, RGB(225, 229, 235)));
            MoveToEx(reinterpret_cast<HDC>(wparam), 0, 86, nullptr);
            LineTo(reinterpret_cast<HDC>(wparam), rect.right, 86);
            MoveToEx(reinterpret_cast<HDC>(wparam), 0, 480, nullptr);
            LineTo(reinterpret_cast<HDC>(wparam), rect.right, 480);
            DeleteObject(SelectObject(reinterpret_cast<HDC>(wparam), old_pen));
            return 1;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        if (state && !state->settings_open) {
            render_splash(*state);
            return 0;
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        if (state && state->settings_open) {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kTextColor);
            return reinterpret_cast<LRESULT>(state->window_brush);
        }
        break;
    case WM_CTLCOLORBTN:
        if (state && state->settings_open) {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(state->window_brush);
        }
        break;
    case WM_LBUTTONUP:
        if (state && !state->settings_open) {
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (PtInRect(&state->splash_settings_rect, point)) {
                show_settings(*state);
                return 0;
            }
        }
        break;
    case WM_NOTIFY:
        if (state && reinterpret_cast<NMHDR*>(lparam)->idFrom == kTab && reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE) {
            select_tab(*state, TabCtrl_GetCurSel(state->tab));
            return 0;
        }
        break;
    case WM_VSCROLL:
        if (state && state->settings_open && state->active_tab == 1) {
            SCROLLINFO scroll{};
            scroll.cbSize = sizeof(scroll);
            scroll.fMask = SIF_ALL;
            GetScrollInfo(window, SB_VERT, &scroll);

            int position = state->tweak_scroll;
            switch (LOWORD(wparam)) {
            case SB_LINEUP:
                position -= kScrollLine;
                break;
            case SB_LINEDOWN:
                position += kScrollLine;
                break;
            case SB_PAGEUP:
                position -= kScrollPage;
                break;
            case SB_PAGEDOWN:
                position += kScrollPage;
                break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                position = scroll.nTrackPos;
                break;
            }
            set_tweak_scroll(*state, position);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (state && state->settings_open && state->active_tab == 1) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            set_tweak_scroll(*state, state->tweak_scroll - (delta / WHEEL_DELTA) * kScrollLine * 3);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (!state) {
            break;
        }
        if (LOWORD(wparam) == kEm4Path && HIWORD(wparam) == EN_CHANGE) {
            update_large_address_status(*state);
            return 0;
        }
        switch (LOWORD(wparam)) {
        case kSettingsButton:
            show_settings(*state);
            return 0;
        case kBrowseButton:
            browse_for_em4(*state);
            return 0;
        case kPatch4GbButton:
            patch_4gb(*state);
            return 0;
        case kRestore4GbButton:
            restore_4gb(*state);
            return 0;
        case kSaveLaunchButton:
            commit_and_finish(*state, GuiResult::Launch);
            return 0;
        case kSaveExitButton:
            commit_and_finish(*state, GuiResult::Cancel);
            return 0;
        case kExitButton:
            finish_without_saving(*state, GuiResult::Cancel);
            return 0;
        }
        break;
    case WM_TIMER:
        if (state && wparam == kTimerId && !state->settings_open) {
            --state->remaining;
            if (state->remaining <= 0) {
                finish_without_saving(*state, GuiResult::Launch);
            } else {
                update_splash_status(*state);
                render_splash(*state);
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        if (state) {
            finish_without_saving(*state, GuiResult::Cancel);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (state) {
            if (state->ui_font) {
                DeleteObject(state->ui_font);
                state->ui_font = nullptr;
            }
            if (state->title_font) {
                DeleteObject(state->title_font);
                state->title_font = nullptr;
            }
            if (state->section_font) {
                DeleteObject(state->section_font);
                state->section_font = nullptr;
            }
            if (state->window_brush) {
                DeleteObject(state->window_brush);
                state->window_brush = nullptr;
            }
            if (state->header_brush) {
                DeleteObject(state->header_brush);
                state->header_brush = nullptr;
            }
        }
        break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

GuiResult show_launcher_window(
    Config& config,
    std::vector<TweakPackage>& packages,
    const std::filesystem::path& config_path) {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gdiplus_input{};
    ULONG_PTR gdiplus_token = 0;
    Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);

    const wchar_t* class_name = L"OMoutaEM4TweaksLauncher";
    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = class_name;
    RegisterClassW(&wc);

    GuiState state;
    state.target_config = &config;
    state.target_packages = &packages;
    state.draft_config = config;
    state.draft_packages = packages;
    state.config_path = config_path;
    state.splash_logo = load_png_resource(instance, IDB_OMouta_TWEAKS_LOGO);

    const int screen_x = GetSystemMetrics(SM_CXSCREEN);
    const int screen_y = GetSystemMetrics(SM_CYSCREEN);
    const int splash_x = std::max(0, (screen_x - kSplashWidth) / 2);
    const int splash_y = std::max(0, (screen_y - kSplashHeight) / 2);

    HWND window = CreateWindowExW(
        WS_EX_LAYERED,
        class_name,
        kBrand,
        WS_POPUP,
        splash_x,
        splash_y,
        kSplashWidth,
        kSplashHeight,
        nullptr,
        nullptr,
        instance,
        &state);

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};
    while (!state.finished && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    state.splash_logo.reset();
    if (gdiplus_token) {
        Gdiplus::GdiplusShutdown(gdiplus_token);
    }

    return state.result;
}

} // namespace om4t::launcher
