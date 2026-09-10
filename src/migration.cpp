#include "migration.hpp"
#include <winioctl.h>
#include <cstring>

namespace softlink {
namespace {
bool isDirectory(DWORD attr) {
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void makeWritable(const fs::path& path) {
    const auto name = windowsPath(path);
    const DWORD attr = GetFileAttributesW(name.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(name.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    }
}

template<class Action>
DWORD retry(Action action, const fs::path& writablePath) {
    DWORD error = ERROR_SUCCESS;
    for (const DWORD delay : {0UL, 200UL, 500UL, 1000UL}) {
        if (delay) Sleep(delay);
        if (action()) return ERROR_SUCCESS;
        error = GetLastError();
        if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION) break;
        makeWritable(writablePath);
    }
    return error;
}

void copyDirectoryMetadata(const fs::path& source, const fs::path& target) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(windowsPath(source).c_str(), GetFileExInfoStandard, &data)) {
        failWin32(L"无法读取目录时间：" + source.wstring());
    }
    Handle handle(CreateFileW(windowsPath(target).c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!handle) failWin32(L"无法设置目录时间：" + target.wstring());
    if (!SetFileTime(handle.get(), &data.ftCreationTime, &data.ftLastAccessTime, &data.ftLastWriteTime)) {
        failWin32(L"无法设置目录时间：" + target.wstring());
    }
    constexpr DWORD mask = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
        FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
    const DWORD attr = data.dwFileAttributes & mask;
    if (!SetFileAttributesW(windowsPath(target).c_str(), attr ? attr : FILE_ATTRIBUTE_NORMAL)) {
        failWin32(L"无法设置目录属性：" + target.wstring());
    }
}

void cloneReparsePoint(const fs::path& source, const fs::path& target, DWORD attr) {
    Handle input(CreateFileW(windowsPath(source).c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!input) failWin32(L"无法读取链接：" + source.wstring());
    std::vector<BYTE> data(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    DWORD size = 0;
    if (!DeviceIoControl(input.get(), FSCTL_GET_REPARSE_POINT, nullptr, 0,
        data.data(), static_cast<DWORD>(data.size()), &size, nullptr)) {
        failWin32(L"无法读取链接：" + source.wstring());
    }
    DWORD tag = 0;
    if (size < sizeof(tag)) fail(L"链接数据不完整：" + source.wstring());
    std::memcpy(&tag, data.data(), sizeof(tag));
    if (tag == IO_REPARSE_TAG_SYMLINK) {
        const auto destination = fs::read_symlink(fs::path(windowsPath(source)));
        const DWORD flags = isDirectory(attr) ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
        if (!CreateSymbolicLinkW(windowsPath(target).c_str(), destination.c_str(), flags)) {
            failWin32(L"无法复制符号链接：" + source.wstring());
        }
        return;
    }
    // Junctions are copied as links; unknown providers must never be traversed or deleted.
    if (tag != IO_REPARSE_TAG_MOUNT_POINT || !isDirectory(attr)) {
        fail(L"不支持的重解析点，已保留原数据：" + source.wstring());
    }
    if (!CreateDirectoryW(windowsPath(target).c_str(), nullptr)) failWin32(L"无法创建目录链接");
    Handle output(CreateFileW(windowsPath(target).c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!output) failWin32(L"无法打开目录链接");
    DWORD returned = 0;
    if (!DeviceIoControl(output.get(), FSCTL_SET_REPARSE_POINT, data.data(), size,
        nullptr, 0, &returned, nullptr)) failWin32(L"无法复制目录链接");
}

void copyEntry(const fs::path& source, const fs::path& target, std::vector<CopyError>& errors);

void copyDirectoryContents(const fs::path& source, const fs::path& target, std::vector<CopyError>& errors) {
    for (const auto& entry : fs::directory_iterator(fs::path(windowsPath(source)))) {
        const auto name = entry.path().filename();
        copyEntry(source / name, target / name, errors);
    }
    copyDirectoryMetadata(source, target);
}

void copyEntry(const fs::path& source, const fs::path& target, std::vector<CopyError>& errors) {
    try {
        const DWORD attr = attributes(source);
        if (attr == INVALID_FILE_ATTRIBUTES) fail(L"复制前文件已不存在");
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
            cloneReparsePoint(source, target, attr);
            return;
        }
        if (isDirectory(attr)) {
            if (!CreateDirectoryW(windowsPath(target).c_str(), nullptr)) failWin32(L"无法创建目标子目录");
            copyDirectoryContents(source, target, errors);
            return;
        }
        const auto input = windowsPath(source);
        const auto output = windowsPath(target);
        const DWORD error = retry([&] { return CopyFileW(input.c_str(), output.c_str(), FALSE); }, target);
        if (error) failWin32(L"文件复制失败", error);
    } catch (const std::exception& error) {
        errors.push_back({source.wstring(), target.wstring(), exceptionText(error)});
    }
}

void removeTree(const fs::path& path) {
    const DWORD attr = attributes(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (isDirectory(attr) && !(attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
        for (const auto& entry : fs::directory_iterator(fs::path(windowsPath(path)))) {
            removeTree(path / entry.path().filename());
        }
    }
    const auto name = windowsPath(path);
    const DWORD error = retry([&] {
        return isDirectory(attr) ? RemoveDirectoryW(name.c_str()) : DeleteFileW(name.c_str());
    }, path);
    if (error) failWin32(L"无法删除备份项：" + path.wstring(), error);
}

fs::path backupPath(const fs::path& source) {
    const auto stem = source.filename().wstring() + L".__softlink_backup_" + timestamp(true);
    fs::path result = source.parent_path() / stem;
    unsigned int suffix = 1;
    while (pathExists(result)) result = source.parent_path() / (stem + L"_" + std::to_wstring(suffix++));
    return result;
}

bool createLink(const fs::path& source, const fs::path& target, std::wstring& output,
    std::wstring& cleanupError) {
    if (!CreateSymbolicLinkW(windowsPath(source).c_str(), windowsPath(target).c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY)) {
        output = errorText(GetLastError());
        return false;
    }
    try {
        if (!samePath(physicalPath(source), physicalPath(target))) fail(L"链接目标校验失败");
        output = L"已创建目录符号链接：" + source.wstring() + L" -> " + target.wstring();
        return true;
    } catch (const std::exception& error) {
        output = exceptionText(error);
    }
    if (!RemoveDirectoryW(windowsPath(source).c_str())) cleanupError = errorText(GetLastError());
    return false;
}

void restoreAccess(Operation& record) {
    if (record.backupPath.empty()) return;
    try {
        if (!pathExists(record.paths.source)) {
            record.fallbackLinkCreated = createLink(record.paths.source, record.backupPath,
                record.fallbackLinkOutput, record.linkCleanupError);
        }
    } catch (const std::exception& error) {
        record.fallbackLinkOutput = exceptionText(error);
    }
    record.message += record.fallbackLinkCreated
        ? L"；原路径已临时链接到保留目录" : L"；原路径未能恢复";
}
}

Paths validatePaths(const std::wstring& sourceText, const std::wstring& targetText, const fs::path& appPath) {
    if (sourceText.find_first_not_of(L" \t\r\n") == std::wstring::npos) fail(L"请选择原目录 A");
    if (targetText.find_first_not_of(L" \t\r\n") == std::wstring::npos) fail(L"请选择目标目录 B");
    Paths paths{absolutePath(sourceText), absolutePath(targetText), {}};
    const DWORD sourceAttr = attributes(paths.source);
    if (!isDirectory(sourceAttr)) fail(L"原目录 A 不存在或不是文件夹");
    if (sourceAttr & FILE_ATTRIBUTE_REPARSE_POINT) fail(L"原目录 A 已经是链接或重解析点，拒绝移动");
    if (paths.source == paths.source.root_path() || paths.source.filename().empty()) fail(L"不能移动磁盘根目录");
    const DWORD targetAttr = attributes(paths.targetRoot);
    if (targetAttr != INVALID_FILE_ATTRIBUTES && !isDirectory(targetAttr)) fail(L"目标路径 B 不是文件夹");
    paths.target = paths.targetRoot / paths.source.filename();
    const auto actualSource = physicalPath(paths.source);
    const auto actualTargetRoot = physicalPath(paths.targetRoot);
    if (samePath(actualSource, actualTargetRoot / paths.source.filename())) fail(L"目标位置与原目录相同");
    if (containsPath(actualSource, actualTargetRoot)) fail(L"目标目录 B 不能位于原目录 A 内部");
    if (containsPath(actualSource, physicalPath(appPath))) fail(L"不能迁移正在运行的程序所在目录，请先将程序放到其他位置");
    if (pathExists(paths.target)) fail(L"目标位置已存在，拒绝覆盖：" + paths.target.wstring());
    return paths;
}

Operation migrate(const Paths& paths, const Progress& progress) {
    Operation record;
    record.paths = paths;
    try {
        validatePaths(paths.source.wstring(), paths.targetRoot.wstring(), executablePath());
        fs::create_directories(fs::path(windowsPath(paths.targetRoot)));
        const auto backup = backupPath(paths.source);
        progress(L"正在改名并复制");
        if (!MoveFileExW(windowsPath(paths.source).c_str(), windowsPath(backup).c_str(), 0)) {
            failWin32(L"原目录改名失败");
        }
        record.backupPath = backup.wstring();
        if (!CreateDirectoryW(windowsPath(paths.target).c_str(), nullptr)) {
            const auto message = L"无法创建目标目录：" + errorText(GetLastError());
            record.copyErrors.push_back({backup.wstring(), paths.target.wstring(), message});
            fail(message);
        }
        record.targetCreated = true;
        try {
            copyDirectoryContents(backup, paths.target, record.copyErrors);
        } catch (const std::exception& error) {
            record.copyErrors.push_back({backup.wstring(), paths.target.wstring(), exceptionText(error)});
        }
        progress(L"正在建立软链接");
        record.linkCreated = createLink(paths.source, paths.target, record.linkOutput, record.linkCleanupError);
        if (!record.linkCreated) fail(L"创建目标链接失败：" + record.linkOutput);
        if (!record.copyErrors.empty()) {
            record.status = L"partial";
            record.message = L"复制有 " + std::to_wstring(record.copyErrors.size()) + L" 个错误，已链接到不完整目标";
            return record;
        }
        progress(L"正在清理备份目录");
        removeTree(backup);
        record.removedExistingSource = true;
        record.status = L"success";
        record.message = L"移动并建立软链接成功";
    } catch (const std::exception& error) {
        record.message = exceptionText(error);
        if (record.linkCreated) {
            record.status = L"success-with-backup";
            record.cleanupError = record.message;
            record.message = L"移动和链接成功，但备份清理未完成：" + record.cleanupError;
        } else {
            restoreAccess(record);
        }
    }
    return record;
}
}
