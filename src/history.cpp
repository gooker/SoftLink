#include "history.hpp"
#pragma warning(push, 0)
#include "third_party/picojson.h"
#pragma warning(pop)
#include <algorithm>
#include <limits>

namespace softlink {
namespace {
picojson::value jsonText(const std::wstring& value) { return picojson::value(utf8(value)); }

picojson::value serializeRecord(const Operation& record) {
    picojson::object object;
    object["timestamp"] = jsonText(timestamp());
    object["source_path"] = jsonText(record.paths.source.wstring());
    object["target_root"] = jsonText(record.paths.targetRoot.wstring());
    object["moved_path"] = jsonText(record.paths.target.wstring());
    object["backup_path"] = jsonText(record.backupPath);
    object["status"] = jsonText(record.status);
    object["message"] = jsonText(record.message);
    object["cleanup_error"] = jsonText(record.cleanupError);
    object["link_cleanup_error"] = jsonText(record.linkCleanupError);
    object["mklink_output"] = jsonText(record.linkOutput);
    object["fallback_mklink_output"] = jsonText(record.fallbackLinkOutput);
    object["removed_existing_source"] = picojson::value(record.removedExistingSource);
    object["link_created"] = picojson::value(record.linkCreated);
    object["fallback_link_created"] = picojson::value(record.fallbackLinkCreated);
    object["target_created"] = picojson::value(record.targetCreated);
    object["copy_error_count"] = picojson::value(static_cast<double>(record.copyErrors.size()));
    picojson::array errors;
    for (const auto& error : record.copyErrors) {
        picojson::object item;
        item["source_path"] = jsonText(error.source);
        item["target_path"] = jsonText(error.target);
        item["message"] = jsonText(error.message);
        errors.emplace_back(item);
    }
    object["copy_errors"] = picojson::value(errors);
    return picojson::value(object);
}

std::wstring column(const picojson::value& value, const char* name) {
    const auto& field = value.get(name);
    return field.is<std::string>() ? wide(field.get<std::string>()) : L"";
}

HistoryRow parseRow(const std::string& line, size_t lineNumber) {
    try {
        picojson::value value;
        auto position = line.begin();
        const auto error = picojson::parse(value, position, line.end());
        const bool trailing = std::any_of(position, line.end(), [](char c) {
            return c != ' ' && c != '\t' && c != '\r' && c != '\n';
        });
        if (!error.empty() || trailing || !value.is<picojson::object>()) {
            throw std::runtime_error("Invalid JSON record");
        }
        return {{column(value, "timestamp"), column(value, "status"), column(value, "source_path"),
            column(value, "moved_path"), column(value, "message")}, wide(value.serialize(true))};
    } catch (const std::exception&) {
        const auto message = L"第 " + std::to_wstring(lineNumber) + L" 行日志损坏";
        return {{L"", L"bad-log", L"", L"", message}, message};
    }
}
}

HistoryWriter::HistoryWriter(const fs::path& path)
    : file_(CreateFileW(windowsPath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) {
    if (!file_) failWin32(L"无法写入操作历史，请将程序放到可写目录：" + path.wstring());
}

void HistoryWriter::append(const Operation& record) {
    const std::string data = serializeRecord(record).serialize() + "\n";
    if (data.size() > std::numeric_limits<DWORD>::max()) fail(L"操作记录过大，无法保存");
    const LARGE_INTEGER offset{};
    if (!SetFilePointerEx(file_.get(), offset, nullptr, FILE_END)) failWin32(L"无法追加操作历史");
    DWORD written = 0;
    if (!WriteFile(file_.get(), data.data(), static_cast<DWORD>(data.size()), &written, nullptr)) {
        failWin32(L"操作历史写入失败");
    }
    if (written != data.size()) fail(L"操作历史未完整写入，请检查磁盘空间");
    if (!FlushFileBuffers(file_.get())) failWin32(L"操作历史未能保存到磁盘");
}

std::vector<HistoryRow> loadHistory(const fs::path& path) {
    Handle file(CreateFileW(windowsPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) return {};
        failWin32(L"无法读取操作历史", error);
    }
    std::vector<HistoryRow> rows;
    std::string pending;
    char buffer[65536];
    DWORD count = 0;
    size_t lineNumber = 0;
    for (;;) {
        if (!ReadFile(file.get(), buffer, sizeof(buffer), &count, nullptr)) failWin32(L"读取操作历史失败");
        pending.append(buffer, count);
        size_t start = 0;
        size_t end = 0;
        while ((end = pending.find('\n', start)) != std::string::npos) {
            const auto line = pending.substr(start, end - start);
            ++lineNumber;
            if (line.find_first_not_of(" \t\r") != std::string::npos) rows.push_back(parseRow(line, lineNumber));
            start = end + 1;
        }
        pending.erase(0, start);
        if (!count) break;
    }
    if (pending.find_first_not_of(" \t\r") != std::string::npos) rows.push_back(parseRow(pending, lineNumber + 1));
    std::reverse(rows.begin(), rows.end());
    return rows;
}
}
