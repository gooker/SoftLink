#include "platform.hpp"
#include <vector>

namespace softlink {
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid Unicode text");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring errorText(DWORD code) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = buffer ? buffer : L"Windows 操作失败";
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result + L" (" + std::to_wstring(code) + L")";
}

std::wstring exceptionText(const std::exception& error) {
    try { return wide(error.what()); }
    catch (...) { return L"操作失败，请检查目录权限和文件占用情况。"; }
}

void fail(const std::wstring& message) { throw std::runtime_error(utf8(message)); }
void failWin32(const std::wstring& action, DWORD code) { fail(action + L"：" + errorText(code)); }

fs::path executablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) failWin32(L"无法确定程序路径");
    return std::wstring(buffer.data(), size);
}

fs::path absolutePath(const std::wstring& text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) fail(L"请选择目录");
    std::wstring input = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
    if (input == L"~" || input.rfind(L"~\\", 0) == 0 || input.rfind(L"~/", 0) == 0) {
        wchar_t profile[32768]{};
        const DWORD size = GetEnvironmentVariableW(L"USERPROFILE", profile, 32768);
        if (!size || size >= 32768) fail(L"无法确定用户目录");
        input = std::wstring(profile) + input.substr(1);
    }
    const DWORD size = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (!size) failWin32(L"无效路径");
    std::vector<wchar_t> buffer(size);
    if (!GetFullPathNameW(input.c_str(), size, buffer.data(), nullptr)) failWin32(L"无效路径");
    fs::path result = fs::path(buffer.data()).lexically_normal();
    while (result != result.root_path() && result.filename().empty()) result = result.parent_path();
    return result;
}

std::wstring windowsPath(const fs::path& path) {
    std::wstring text = path.wstring();
    if (text.rfind(L"\\\\?\\", 0) == 0) return text;
    if (text.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + text.substr(2);
    return L"\\\\?\\" + text;
}

DWORD attributes(const fs::path& path) {
    const DWORD result = GetFileAttributesW(windowsPath(path).c_str());
    if (result != INVALID_FILE_ATTRIBUTES) return result;
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return result;
    failWin32(L"无法读取目录或文件：" + path.wstring(), error);
}

bool pathExists(const fs::path& path) { return attributes(path) != INVALID_FILE_ATTRIBUTES; }

bool samePath(const fs::path& first, const fs::path& second) {
    const auto left = first.lexically_normal().wstring();
    const auto right = second.lexically_normal().wstring();
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool containsPath(const fs::path& parent, const fs::path& child) {
    for (fs::path current = child; !current.empty();) {
        if (samePath(parent, current)) return true;
        const auto next = current.parent_path();
        if (next == current) break;
        current = next;
    }
    return false;
}

fs::path physicalPath(const fs::path& path) {
    fs::path existing = path;
    fs::path suffix;
    while (!pathExists(existing)) {
        if (existing == existing.root_path() || existing.empty()) fail(L"路径根目录不存在");
        suffix = existing.filename() / suffix;
        existing = existing.parent_path();
    }
    Handle handle(CreateFileW(windowsPath(existing).c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!handle) failWin32(L"无法解析实际目录：" + existing.wstring());
    const DWORD size = GetFinalPathNameByHandleW(handle.get(), nullptr, 0, FILE_NAME_NORMALIZED);
    if (!size) failWin32(L"无法解析实际目录");
    std::vector<wchar_t> buffer(size + 1);
    if (!GetFinalPathNameByHandleW(handle.get(), buffer.data(), size + 1, FILE_NAME_NORMALIZED)) {
        failWin32(L"无法解析实际目录");
    }
    fs::path result = fs::path(buffer.data()) / suffix;
    while (result != result.root_path() && result.filename().empty()) result = result.parent_path();
    return result.lexically_normal();
}

std::wstring timestamp(bool filename) {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[32]{};
    swprintf_s(buffer, filename ? L"%04u%02u%02u_%02u%02u%02u" : L"%04u-%02u-%02uT%02u:%02u:%02u",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return buffer;
}
}
