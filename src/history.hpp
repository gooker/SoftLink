#pragma once
#include "migration.hpp"
#include <array>

namespace softlink {
struct HistoryRow {
    std::array<std::wstring, 5> columns;
    std::wstring detail;
};

class HistoryWriter {
public:
    explicit HistoryWriter(const fs::path& path);
    void append(const Operation& record);
private:
    Handle file_;
};

std::vector<HistoryRow> loadHistory(const fs::path& path);
}
