#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace softlink {
namespace fs = std::filesystem;

class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~Handle() { if (*this) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};

std::string utf8(const std::wstring& value);
std::wstring wide(const std::string& value);
std::wstring errorText(DWORD code);
std::wstring exceptionText(const std::exception& error);
[[noreturn]] void fail(const std::wstring& message);
[[noreturn]] void failWin32(const std::wstring& action, DWORD code = GetLastError());
fs::path executablePath();
fs::path absolutePath(const std::wstring& text);
std::wstring windowsPath(const fs::path& path);
DWORD attributes(const fs::path& path);
bool pathExists(const fs::path& path);
bool samePath(const fs::path& first, const fs::path& second);
bool containsPath(const fs::path& parent, const fs::path& child);
fs::path physicalPath(const fs::path& path);
std::wstring timestamp(bool filename = false);
}
