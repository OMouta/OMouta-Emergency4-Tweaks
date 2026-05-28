#include "GuiLauncher.h"

#include "../shared/AppPaths.h"
#include "../shared/StringUtil.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <array>

namespace om4t::launcher {

namespace {

constexpr int kTimerId = 1001;
constexpr int kCountdownSeconds = 5;
constexpr int kSettingsButton = 2001;
constexpr int kBrowseButton = 2002;
constexpr int kSaveLaunchButton = 2003;
constexpr int kSaveExitButton = 2004;
constexpr int kExitButton = 2005;
constexpr int kEm4Path = 2100;
constexpr int kTab = 2200;
constexpr int kTweakBase = 3000;

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
    int remaining = kCountdownSeconds;
    HWND window = nullptr;
    HWND splash_icon = nullptr;
    HWND splash_title = nullptr;
    HWND splash_status = nullptr;
    HWND splash_settings = nullptr;
    HWND splash_progress = nullptr;
    HWND tab = nullptr;
    HWND em4_path = nullptr;
    std::vector<HWND> general_controls;
    std::vector<HWND> tweak_controls;
    std::vector<HWND> about_controls;
    std::vector<HWND> action_controls;
    std::vector<HWND> tweak_checks;
    std::vector<SettingControl> setting_controls;
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

HWND make_control(HWND parent, const wchar_t* klass, const std::wstring& text, DWORD style, int id, int x, int y, int w, int h) {
    HWND control = CreateWindowExW(
        0,
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
    set_font(control, reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)));
    return control;
}

HWND make_edit(HWND parent, const std::wstring& text, int id, int x, int y, int w, int h) {
    return make_control(parent, L"EDIT", text, WS_BORDER | ES_AUTOHSCROLL, id, x, y, w, h);
}

void set_visible(const std::vector<HWND>& controls, bool visible) {
    for (HWND control : controls) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    }
}

std::wstring setting_value(const Config& config, const TweakSetting& setting) {
    if (auto section = config.sections.find(setting.section); section != config.sections.end()) {
        if (auto value = section->second.find(setting.key); value != section->second.end()) {
            return value->second;
        }
    }
    return setting.default_value;
}

void update_splash_status(GuiState& state) {
    SetWindowTextW(state.splash_status, (L"Launching in " + std::to_wstring(state.remaining) + L" seconds").c_str());
    SendMessageW(state.splash_progress, PBM_SETRANGE, 0, MAKELPARAM(0, kCountdownSeconds));
    SendMessageW(state.splash_progress, PBM_SETPOS, kCountdownSeconds - state.remaining, 0);
}

void select_tab(GuiState& state, int index) {
    set_visible(state.general_controls, index == 0);
    set_visible(state.tweak_controls, index == 1);
    set_visible(state.about_controls, index == 2);
}

void show_settings(GuiState& state) {
    state.settings_open = true;
    KillTimer(state.window, kTimerId);
    ShowWindow(state.splash_icon, SW_HIDE);
    ShowWindow(state.splash_title, SW_HIDE);
    ShowWindow(state.splash_status, SW_HIDE);
    ShowWindow(state.splash_settings, SW_HIDE);
    ShowWindow(state.splash_progress, SW_HIDE);
    ShowWindow(state.tab, SW_SHOW);
    set_visible(state.action_controls, true);
    TabCtrl_SetCurSel(state.tab, 0);
    select_tab(state, 0);
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

    sync_legacy_fields_from_sections(state.draft_config);
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
    state.splash_icon = make_control(window, L"STATIC", L"", SS_ICON | SS_CENTERIMAGE, 0, 60, 56, 96, 96);
    HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    SendMessageW(state.splash_icon, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);
    state.splash_title = make_control(window, L"STATIC", kBrand, SS_LEFT, 0, 176, 72, 340, 28);
    state.splash_status = make_control(window, L"STATIC", L"", SS_LEFT, 0, 176, 106, 240, 22);
    state.splash_settings = make_control(window, L"BUTTON", L"Settings", BS_PUSHBUTTON, kSettingsButton, 426, 98, 96, 30);
    state.splash_progress = make_control(window, PROGRESS_CLASSW, L"", 0, 0, 176, 136, 346, 16);
    update_splash_status(state);
}

void add_general_controls(HWND window, GuiState& state) {
    state.general_controls.push_back(make_control(window, L"STATIC", L"Game executable", SS_LEFT, 0, 34, 76, 180, 20));
    state.em4_path = make_edit(window, state.draft_config.em4_path.wstring(), kEm4Path, 34, 100, 480, 24);
    state.general_controls.push_back(state.em4_path);
    state.general_controls.push_back(make_control(window, L"BUTTON", L"Browse", BS_PUSHBUTTON, kBrowseButton, 524, 99, 92, 26));
}

void add_tweak_controls(HWND window, GuiState& state) {
    int y = 76;
    if (state.draft_packages.empty()) {
        state.tweak_controls.push_back(make_control(window, L"STATIC", L"No tweak packages were found.", SS_LEFT, 0, 34, y, 520, 22));
        return;
    }

    for (size_t i = 0; i < state.draft_packages.size(); ++i) {
        auto& package = state.draft_packages[i];
        HWND check = make_control(window, L"BUTTON", package.name, BS_AUTOCHECKBOX, kTweakBase + static_cast<int>(i), 34, y, 300, 22);
        SendMessageW(check, BM_SETCHECK, package.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        state.tweak_checks.push_back(check);
        state.tweak_controls.push_back(check);

        if (!package.description.empty()) {
            state.tweak_controls.push_back(make_control(window, L"STATIC", package.description, SS_LEFT, 0, 54, y + 24, 560, 20));
            y += 48;
        } else {
            y += 28;
        }

        for (size_t setting_index = 0; setting_index < package.settings.size(); ++setting_index) {
            const auto& setting = package.settings[setting_index];
            const std::wstring value = setting_value(state.draft_config, setting);
            state.tweak_controls.push_back(make_control(window, L"STATIC", setting.label, SS_LEFT, 0, 54, y + 3, 230, 20));
            HWND control = nullptr;
            if (setting.type == L"bool") {
                control = make_control(window, L"BUTTON", L"", BS_AUTOCHECKBOX, 0, 292, y, 22, 22);
                SendMessageW(control, BM_SETCHECK, parse_bool(value, parse_bool(setting.default_value, false)) ? BST_CHECKED : BST_UNCHECKED, 0);
            } else {
                DWORD style = WS_BORDER | ES_AUTOHSCROLL;
                if (setting.type == L"int") {
                    style |= ES_NUMBER;
                }
                control = make_control(window, L"EDIT", value, style, 0, 292, y, 120, 24);
            }
            state.tweak_controls.push_back(control);
            state.setting_controls.push_back(SettingControl{i, setting_index, control});
            y += 30;
        }

        y += 12;
    }
}

void add_about_controls(HWND window, GuiState& state) {
    state.about_controls.push_back(make_control(window, L"STATIC", kBrand, SS_LEFT, 0, 34, 78, 420, 24));
    state.about_controls.push_back(make_control(window, L"STATIC", L"Native launcher for EMERGENCY 4 tweak packages.", SS_LEFT, 0, 34, 110, 520, 22));
}

void layout_settings(HWND window, GuiState& state) {
    state.tab = make_control(window, WC_TABCONTROLW, L"", WS_CLIPSIBLINGS, kTab, 18, 44, 622, 334);
    add_tab(state.tab, L"General", 0);
    add_tab(state.tab, L"Tweaks", 1);
    add_tab(state.tab, L"About", 2);

    add_general_controls(window, state);
    add_tweak_controls(window, state);
    add_about_controls(window, state);

    state.action_controls.push_back(make_control(window, L"BUTTON", L"Save && Launch", BS_DEFPUSHBUTTON, kSaveLaunchButton, 306, 398, 112, 30));
    state.action_controls.push_back(make_control(window, L"BUTTON", L"Save && Exit", BS_PUSHBUTTON, kSaveExitButton, 424, 398, 100, 30));
    state.action_controls.push_back(make_control(window, L"BUTTON", L"Exit", BS_PUSHBUTTON, kExitButton, 530, 398, 82, 30));

    ShowWindow(state.tab, SW_HIDE);
    set_visible(state.general_controls, false);
    set_visible(state.tweak_controls, false);
    set_visible(state.about_controls, false);
    set_visible(state.action_controls, false);
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
        layout_settings(window, *state);
        SetTimer(window, kTimerId, 1000, nullptr);
        return 0;
    case WM_NOTIFY:
        if (state && reinterpret_cast<NMHDR*>(lparam)->idFrom == kTab && reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE) {
            select_tab(*state, TabCtrl_GetCurSel(state->tab));
            return 0;
        }
        break;
    case WM_COMMAND:
        if (!state) {
            break;
        }
        switch (LOWORD(wparam)) {
        case kSettingsButton:
            show_settings(*state);
            return 0;
        case kBrowseButton:
            browse_for_em4(*state);
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

    HWND window = CreateWindowExW(
        0,
        class_name,
        kBrand,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        674,
        482,
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

    return state.result;
}

} // namespace om4t::launcher
