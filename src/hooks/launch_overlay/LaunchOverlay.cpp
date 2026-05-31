#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "LaunchOverlay.h"

#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace {

constexpr const wchar_t* kDataDir = L"OMoutaEM4Tweaks";
constexpr const wchar_t* kConfigName = L"config.ini";
constexpr int kOverlayWidth = 620;
constexpr int kOverlayHeight = 260;

struct OverlayConfig {
    int duration_ms = 4500;
    std::vector<std::wstring> loaded_tweaks;
};

std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::map<std::wstring, std::map<std::wstring, std::wstring>> read_ini(const std::filesystem::path& path) {
    std::map<std::wstring, std::map<std::wstring, std::wstring>> sections;
    std::wifstream input(path);
    std::wstring section;
    std::wstring line;

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';') {
            continue;
        }
        if (line.front() == L'[' && line.back() == L']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }
        sections[section][trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }

    return sections;
}

int parse_int(const std::wstring& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::filesystem::path process_directory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

std::vector<std::wstring> split_list(const std::wstring& value) {
    std::vector<std::wstring> items;
    size_t start = 0;
    while (start <= value.size()) {
        const auto separator = value.find(L'|', start);
        auto item = trim(value.substr(start, separator == std::wstring::npos ? std::wstring::npos : separator - start));
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
        if (separator == std::wstring::npos) {
            break;
        }
        start = separator + 1;
    }
    return items;
}

OverlayConfig load_config() {
    OverlayConfig config;
    const auto sections = read_ini(process_directory() / kDataDir / kConfigName);

    if (auto section = sections.find(L"LaunchOverlay"); section != sections.end()) {
        if (auto it = section->second.find(L"duration_ms"); it != section->second.end()) {
            config.duration_ms = std::clamp(parse_int(it->second, config.duration_ms), 1000, 15000);
        }
        if (auto it = section->second.find(L"loaded_tweaks"); it != section->second.end()) {
            config.loaded_tweaks = split_list(it->second);
        }
    }

    if (config.loaded_tweaks.empty()) {
        config.loaded_tweaks.push_back(L"No tweaks reported");
    }

    return config;
}

LRESULT CALLBACK overlay_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_TIMER) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void draw_overlay(HWND window, const OverlayConfig& config) {
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = kOverlayWidth;
    info.bmiHeader.biHeight = -kOverlayHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old_bitmap = SelectObject(memory, bitmap);

    Gdiplus::Graphics graphics(memory);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    Gdiplus::GraphicsPath panel;
    panel.AddArc(0, 0, 24, 24, 180, 90);
    panel.AddArc(kOverlayWidth - 24, 0, 24, 24, 270, 90);
    panel.AddArc(kOverlayWidth - 24, kOverlayHeight - 24, 24, 24, 0, 90);
    panel.AddArc(0, kOverlayHeight - 24, 24, 24, 90, 90);
    panel.CloseFigure();

    Gdiplus::SolidBrush panel_brush(Gdiplus::Color(225, 18, 20, 26));
    Gdiplus::Pen panel_border(Gdiplus::Color(120, 255, 255, 255), 1.0f);
    graphics.FillPath(&panel_brush, &panel);
    graphics.DrawPath(&panel_border, &panel);

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font title_font(&family, 28.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font label_font(&family, 15.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font item_font(&family, 17.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush title_brush(Gdiplus::Color(245, 255, 255, 255));
    Gdiplus::SolidBrush muted_brush(Gdiplus::Color(190, 210, 216, 226));
    Gdiplus::SolidBrush item_brush(Gdiplus::Color(235, 255, 255, 255));

    Gdiplus::RectF title_rect(34.0f, 28.0f, 552.0f, 36.0f);
    graphics.DrawString(L"OMoutaEM4Tweaks", -1, &title_font, title_rect, nullptr, &title_brush);
    Gdiplus::RectF label_rect(36.0f, 74.0f, 552.0f, 24.0f);
    graphics.DrawString(L"Loaded tweaks", -1, &label_font, label_rect, nullptr, &muted_brush);

    int y = 108;
    const size_t max_items = std::min<size_t>(config.loaded_tweaks.size(), 5);
    for (size_t i = 0; i < max_items; ++i) {
        const std::wstring line = L"- " + config.loaded_tweaks[i];
        Gdiplus::RectF item_rect(54.0f, static_cast<Gdiplus::REAL>(y), 520.0f, 24.0f);
        graphics.DrawString(line.c_str(), -1, &item_font, item_rect, nullptr, &item_brush);
        y += 26;
    }
    if (config.loaded_tweaks.size() > max_items) {
        const std::wstring more = L"- +" + std::to_wstring(config.loaded_tweaks.size() - max_items) + L" more";
        Gdiplus::RectF item_rect(54.0f, static_cast<Gdiplus::REAL>(y), 520.0f, 24.0f);
        graphics.DrawString(more.c_str(), -1, &item_font, item_rect, nullptr, &muted_brush);
    }

    POINT source{0, 0};
    SIZE size{kOverlayWidth, kOverlayHeight};
    POINT destination{};
    RECT desktop{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &desktop, 0);
    destination.x = desktop.left + ((desktop.right - desktop.left) - kOverlayWidth) / 2;
    destination.y = desktop.top + 64;
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, screen, &destination, &size, memory, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

DWORD WINAPI worker_thread(LPVOID) {
    Sleep(900);

    Gdiplus::GdiplusStartupInput gdiplus_input{};
    ULONG_PTR gdiplus_token = 0;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Gdiplus::Ok) {
        return 1;
    }

    const auto config = load_config();
    HINSTANCE instance = GetModuleHandleW(L"LaunchOverlay.dll");
    const wchar_t* class_name = L"OMoutaLaunchOverlay";

    WNDCLASSW wc{};
    wc.lpfnWndProc = overlay_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = class_name;
    RegisterClassW(&wc);

    HWND window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        class_name,
        L"OMoutaEM4Tweaks",
        WS_POPUP,
        0,
        0,
        kOverlayWidth,
        kOverlayHeight,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window) {
        draw_overlay(window, config);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        SetTimer(window, 1, static_cast<UINT>(config.duration_ms), nullptr);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    Gdiplus::GdiplusShutdown(gdiplus_token);
    return 0;
}

} // namespace

namespace om4t::hooks::launch_overlay {

void start_launch_overlay(HMODULE module) {
    DisableThreadLibraryCalls(module);
    HANDLE thread = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
}

} // namespace om4t::hooks::launch_overlay
