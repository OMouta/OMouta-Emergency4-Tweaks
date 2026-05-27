#include "GuiLauncher.h"

#include "../shared/AppPaths.h"
#include "../shared/StringUtil.h"

#include <windows.h>
#include <commdlg.h>

#include <array>

namespace om4t::launcher {

namespace {

constexpr int kTimerId = 1001;
constexpr int kCountdownSeconds = 5;
constexpr int kLaunchButton = 2001;
constexpr int kSaveButton = 2002;
constexpr int kCancelButton = 2003;
constexpr int kBrowseButton = 2004;
constexpr int kKeepVisible = 2005;
constexpr int kEm4Path = 2100;
constexpr int kRectX = 2101;
constexpr int kRectY = 2102;
constexpr int kRectW = 2103;
constexpr int kRectH = 2104;
constexpr int kTweakBase = 3000;

struct GuiState {
    Config* config = nullptr;
    std::vector<TweakPackage>* packages = nullptr;
    std::filesystem::path config_path;
    GuiResult result = GuiResult::Cancel;
    bool finished = false;
    bool countdown_active = true;
    int remaining = kCountdownSeconds;
    HWND window = nullptr;
    HWND status = nullptr;
    HWND em4_path = nullptr;
    HWND rect_x = nullptr;
    HWND rect_y = nullptr;
    HWND rect_w = nullptr;
    HWND rect_h = nullptr;
    HWND keep_visible = nullptr;
    std::vector<HWND> tweak_checks;
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

void set_status(GuiState& state) {
    std::wstring text;
    if (state.countdown_active) {
        text = L"Launching in " + std::to_wstring(state.remaining) + L" seconds";
    } else {
        text = L"Ready";
    }
    SetWindowTextW(state.status, text.c_str());
}

void stop_countdown(GuiState& state) {
    if (!state.countdown_active) {
        return;
    }
    state.countdown_active = false;
    KillTimer(state.window, kTimerId);
    set_status(state);
}

int edit_int(HWND control, int fallback) {
    return parse_int(trim(window_text(control)), fallback);
}

void read_controls(GuiState& state) {
    auto& config = *state.config;
    auto& packages = *state.packages;

    config.em4_path = trim(window_text(state.em4_path));
    config.borderless_x = edit_int(state.rect_x, config.borderless_x);
    config.borderless_y = edit_int(state.rect_y, config.borderless_y);
    config.borderless_width = edit_int(state.rect_w, config.borderless_width);
    config.borderless_height = edit_int(state.rect_h, config.borderless_height);
    config.keep_visible_on_focus_loss = SendMessageW(state.keep_visible, BM_GETCHECK, 0, 0) == BST_CHECKED;

    for (size_t i = 0; i < packages.size(); ++i) {
        packages[i].enabled = SendMessageW(state.tweak_checks[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    sync_config_from_packages(packages, config);
}

void finish(GuiState& state, GuiResult result) {
    read_controls(state);
    write_config(state.config_path, *state.config);
    state.result = result;
    state.finished = true;
    DestroyWindow(state.window);
}

void browse_for_em4(GuiState& state) {
    stop_countdown(state);

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

void layout_controls(HWND window, GuiState& state) {
    make_control(window, L"STATIC", kBrand, SS_LEFT, 0, 18, 16, 560, 24);
    state.status = make_control(window, L"STATIC", L"", SS_LEFT, 0, 18, 44, 260, 22);

    make_control(window, L"STATIC", L"Game executable", SS_LEFT, 0, 18, 82, 160, 20);
    state.em4_path = make_control(window, L"EDIT", state.config->em4_path.wstring(), WS_BORDER | ES_AUTOHSCROLL, kEm4Path, 18, 104, 460, 24);
    make_control(window, L"BUTTON", L"Browse", BS_PUSHBUTTON, kBrowseButton, 488, 103, 92, 26);

    make_control(window, L"STATIC", L"Tweaks", SS_LEFT, 0, 18, 148, 160, 20);
    int y = 172;
    for (size_t i = 0; i < state.packages->size(); ++i) {
        const auto& package = (*state.packages)[i];
        HWND check = make_control(
            window,
            L"BUTTON",
            package.name,
            BS_AUTOCHECKBOX,
            kTweakBase + static_cast<int>(i),
            18,
            y,
            270,
            22);
        SendMessageW(check, BM_SETCHECK, package.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        state.tweak_checks.push_back(check);

        if (!package.description.empty()) {
            make_control(window, L"STATIC", package.description, SS_LEFT, 0, 38, y + 24, 520, 20);
            y += 48;
        } else {
            y += 28;
        }
    }

    make_control(window, L"STATIC", L"Borderless window rectangle", SS_LEFT, 0, 18, y + 8, 220, 20);
    state.rect_x = make_control(window, L"EDIT", std::to_wstring(state.config->borderless_x), WS_BORDER | ES_NUMBER, kRectX, 18, y + 32, 70, 24);
    state.rect_y = make_control(window, L"EDIT", std::to_wstring(state.config->borderless_y), WS_BORDER | ES_NUMBER, kRectY, 98, y + 32, 70, 24);
    state.rect_w = make_control(window, L"EDIT", std::to_wstring(state.config->borderless_width), WS_BORDER | ES_NUMBER, kRectW, 178, y + 32, 80, 24);
    state.rect_h = make_control(window, L"EDIT", std::to_wstring(state.config->borderless_height), WS_BORDER | ES_NUMBER, kRectH, 268, y + 32, 80, 24);
    make_control(window, L"STATIC", L"X", SS_CENTER, 0, 18, y + 58, 70, 18);
    make_control(window, L"STATIC", L"Y", SS_CENTER, 0, 98, y + 58, 70, 18);
    make_control(window, L"STATIC", L"Width", SS_CENTER, 0, 178, y + 58, 80, 18);
    make_control(window, L"STATIC", L"Height", SS_CENTER, 0, 268, y + 58, 80, 18);

    state.keep_visible = make_control(window, L"BUTTON", L"Keep game visible when focus changes", BS_AUTOCHECKBOX, kKeepVisible, 18, y + 84, 300, 22);
    SendMessageW(state.keep_visible, BM_SETCHECK, state.config->keep_visible_on_focus_loss ? BST_CHECKED : BST_UNCHECKED, 0);

    make_control(window, L"BUTTON", L"Launch", BS_DEFPUSHBUTTON, kLaunchButton, 306, 344, 88, 30);
    make_control(window, L"BUTTON", L"Save", BS_PUSHBUTTON, kSaveButton, 400, 344, 82, 30);
    make_control(window, L"BUTTON", L"Cancel", BS_PUSHBUTTON, kCancelButton, 488, 344, 82, 30);

    set_status(state);
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
        layout_controls(window, *state);
        SetTimer(window, kTimerId, 1000, nullptr);
        return 0;
    case WM_COMMAND:
        if (!state) {
            break;
        }
        switch (LOWORD(wparam)) {
        case kLaunchButton:
            finish(*state, GuiResult::Launch);
            return 0;
        case kSaveButton:
            stop_countdown(*state);
            read_controls(*state);
            write_config(state->config_path, *state->config);
            set_status(*state);
            return 0;
        case kCancelButton:
            state->result = GuiResult::Cancel;
            state->finished = true;
            DestroyWindow(window);
            return 0;
        case kBrowseButton:
            browse_for_em4(*state);
            return 0;
        default:
            if (HIWORD(wparam) == EN_CHANGE || HIWORD(wparam) == BN_CLICKED) {
                stop_countdown(*state);
            }
            return 0;
        }
        break;
    case WM_TIMER:
        if (state && wparam == kTimerId && state->countdown_active) {
            --state->remaining;
            if (state->remaining <= 0) {
                finish(*state, GuiResult::Launch);
            } else {
                set_status(*state);
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        if (state) {
            state->result = GuiResult::Cancel;
            state->finished = true;
        }
        DestroyWindow(window);
        return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

GuiResult show_launcher_window(
    Config& config,
    std::vector<TweakPackage>& packages,
    const std::filesystem::path& config_path) {
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
    state.config = &config;
    state.packages = &packages;
    state.config_path = config_path;

    HWND window = CreateWindowExW(
        0,
        class_name,
        kBrand,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        620,
        430,
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
