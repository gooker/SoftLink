#pragma once
#include "platform.hpp"
#include <functional>
#include <vector>

namespace softlink {
struct Paths {
    fs::path source;
    fs::path targetRoot;
    fs::path target;
};

struct CopyError {
    std::wstring source;
    std::wstring target;
    std::wstring message;
};

struct Operation {
    Paths paths;
    std::wstring backupPath;
    std::wstring status = L"failed";
    std::wstring message;
    std::wstring cleanupError;
    std::wstring linkCleanupError;
    std::wstring linkOutput;
    std::wstring fallbackLinkOutput;
    std::vector<CopyError> copyErrors;
    bool removedExistingSource = false;
    bool linkCreated = false;
    bool fallbackLinkCreated = false;
    bool targetCreated = false;
};

using Progress = std::function<void(const std::wstring&)>;
Paths validatePaths(const std::wstring& source, const std::wstring& targetRoot, const fs::path& appPath);
Operation migrate(const Paths& paths, const Progress& progress);
}
