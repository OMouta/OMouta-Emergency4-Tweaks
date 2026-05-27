#pragma once

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace om4t {

class Logger {
public:
    Logger() = default;
    Logger(const std::filesystem::path& path, std::wstring source)
        : source_(std::move(source)) {
        open(path);
    }

    void open(const std::filesystem::path& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        stream_.open(path, std::ios::app);
    }

    void write(const std::wstring& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stream_.is_open()) {
            return;
        }

        SYSTEMTIME st{};
        GetLocalTime(&st);
        stream_ << L"[" << st.wYear << L"-" << pad(st.wMonth) << L"-" << pad(st.wDay)
                << L" " << pad(st.wHour) << L":" << pad(st.wMinute) << L":" << pad(st.wSecond)
                << L"] [" << source_ << L"] " << message << L"\n";
        stream_.flush();
    }

    void last_error(const std::wstring& context) {
        write(context + L" GetLastError=" + std::to_wstring(GetLastError()));
    }

private:
    static std::wstring pad(WORD value) {
        return value < 10 ? L"0" + std::to_wstring(value) : std::to_wstring(value);
    }

    std::wofstream stream_;
    std::wstring source_ = L"Unknown";
    std::mutex mutex_;
};

} // namespace om4t
