#include "history.hpp"
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <algorithm>
#include <memory>
#include <thread>

namespace softlink {
namespace {
constexpr wchar_t title[] = L"SoftLink Folder Mover";
constexpr wchar_t mainClass[] = L"ToolHub.SoftLink.Main";
constexpr wchar_t historyClass[] = L"ToolHub.SoftLink.History";
constexpr UINT progressMessage = WM_APP + 1;
constexpr UINT finishedMessage = WM_APP + 2;
constexpr UINT licenseCommand = 0x100;
enum ControlId { sourceEdit = 1000, targetEdit, sourceBrowse, targetBrowse, moveButton,
    historyButton, clearButton, helpButton, historyList };

struct Result {
    Operation operation;
    std::wstring logError;
};

std::wstring windowText(HWND window) {
    const int size = GetWindowTextLengthW(window);
    std::wstring text(static_cast<size_t>(size) + 1, L'\0');
    text.resize(GetWindowTextW(window, text.data(), size + 1));
    return text;
}

std::wstring crlf(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size());
    wchar_t previous = 0;
    for (const auto ch : text) {
        if (ch == L'\n' && previous != L'\r') result += L'\r';
        result += ch;
        previous = ch;
    }
    return result;
}

HFONT makeFont(UINT dpi) {
    LOGFONTW description{};
    description.lfHeight = -MulDiv(9, dpi, 72);
    description.lfWeight = FW_NORMAL;
    description.lfCharSet = DEFAULT_CHARSET;
    description.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(description.lfFaceName, L"Microsoft YaHei UI");
    HFONT font = CreateFontIndirectW(&description);
    if (!font) failWin32(L"无法创建界面字体");
    return font;
}

void setChildrenFont(HWND window, HFONT font) {
    EnumChildWindows(window, [](HWND child, LPARAM value) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(value), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

HWND control(HWND parent, HFONT font, const wchar_t* kind, const wchar_t* text,
    DWORD style, int id = 0, DWORD extended = 0) {
    HWND window = CreateWindowExW(extended, kind, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (!window) failWin32(L"无法创建界面控件");
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return window;
}

void place(HWND child, int x, int y, int width, int height, UINT dpi) {
    MoveWindow(child, MulDiv(x, dpi, 96), MulDiv(y, dpi, 96),
        MulDiv(std::max(1, width), dpi, 96), MulDiv(std::max(1, height), dpi, 96), TRUE);
}

void padTextArea(HWND window, UINT dpi) {
    RECT rect{};
    GetClientRect(window, &rect);
    const int padding = MulDiv(4, dpi, 96);
    InflateRect(&rect, -padding, -padding);
    SendMessageW(window, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&rect));
}

bool administrator() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return false;
    Handle token(raw);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size)
        && elevation.TokenIsElevated;
}

void showLicense(HWND owner) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(102), RT_RCDATA);
    if (!resource) failWin32(L"无法读取许可信息");
    HGLOBAL loaded = LoadResource(module, resource);
    const auto* bytes = static_cast<const char*>(LockResource(loaded));
    if (!bytes) failWin32(L"无法读取许可信息");
    const auto text = crlf(wide(std::string(bytes, SizeofResource(module, resource))));
    MessageBoxW(owner, text.c_str(), L"第三方许可：picojson", MB_OK | MB_ICONINFORMATION);
}

class App {
public:
    ~App() {
        if (worker_.joinable()) worker_.join();
        if (font_) DeleteObject(font_);
        if (historyFont_) DeleteObject(historyFont_);
    }

    HWND window = nullptr;
    HWND historyWindow = nullptr;

    void initialize(HWND handle) {
        window = handle;
        dpi_ = GetDpiForWindow(window);
        font_ = makeFont(dpi_);
        appPath_ = executablePath();
        logPath_ = appPath_.parent_path() / L"operations.jsonl";
        admin_ = control(window, font_, L"STATIC", L"管理员状态：已获得管理员权限", SS_CENTERIMAGE);
        sourceLabel_ = control(window, font_, L"STATIC", L"原目录 A", SS_CENTERIMAGE);
        targetLabel_ = control(window, font_, L"STATIC", L"目标目录 B", SS_CENTERIMAGE);
        source_ = control(window, font_, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, sourceEdit, WS_EX_CLIENTEDGE);
        target_ = control(window, font_, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, targetEdit, WS_EX_CLIENTEDGE);
        sourceBrowse_ = control(window, font_, L"BUTTON", L"选择", BS_PUSHBUTTON | WS_TABSTOP, sourceBrowse);
        targetBrowse_ = control(window, font_, L"BUTTON", L"选择", BS_PUSHBUTTON | WS_TABSTOP, targetBrowse);
        move_ = control(window, font_, L"BUTTON", L"移动并建立软链接", BS_PUSHBUTTON | WS_TABSTOP, moveButton);
        history_ = control(window, font_, L"BUTTON", L"查看历史记录", BS_PUSHBUTTON | WS_TABSTOP, historyButton);
        clear_ = control(window, font_, L"BUTTON", L"清空状态输出", BS_PUSHBUTTON | WS_TABSTOP, clearButton);
        help_ = control(window, font_, L"BUTTON", L"使用说明", BS_PUSHBUTTON | WS_TABSTOP, helpButton);
        status_ = control(window, font_, L"STATIC", L"就绪", SS_CENTERIMAGE);
        progress_ = control(window, font_, PROGRESS_CLASSW, L"", 0);
        output_ = control(window, font_, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
            WS_VSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE);
        SendMessageW(output_, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        SendMessageW(source_, EM_SETLIMITTEXT, 32760, 0);
        SendMessageW(target_, EM_SETLIMITTEXT, 32760, 0);
        SendMessageW(progress_, PBM_SETPOS, 0, 0);
        HMENU menu = GetSystemMenu(window, FALSE);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, licenseCommand, L"第三方许可...");
        layout();
    }

    void layout() {
        if (!output_) return;
        RECT rect{};
        GetClientRect(window, &rect);
        const int width = MulDiv(rect.right, 96, dpi_);
        const int height = MulDiv(rect.bottom, 96, dpi_);
        place(admin_, 8, 8, width - 16, 24, dpi_);
        place(sourceLabel_, 8, 46, 64, 24, dpi_);
        place(targetLabel_, 8, 80, 64, 24, dpi_);
        place(source_, 78, 46, width - 196, 24, dpi_);
        place(target_, 78, 80, width - 196, 24, dpi_);
        place(sourceBrowse_, width - 112, 46, 104, 24, dpi_);
        place(targetBrowse_, width - 112, 80, 104, 24, dpi_);
        place(move_, 8, 116, 112, 24, dpi_);
        place(history_, 126, 116, 88, 24, dpi_);
        place(clear_, 220, 116, 88, 24, dpi_);
        place(help_, 314, 116, 88, 24, dpi_);
        place(status_, 8, 148, 102, 22, dpi_);
        place(progress_, 116, 148, width - 124, 22, dpi_);
        place(output_, 8, 176, width - 16, height - 184, dpi_);
        padTextArea(output_, dpi_);
    }

    LRESULT textAreaColor(HDC dc, HWND child) const {
        if (child != output_ && child != detail_) return 0;
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }

    void changeDpi(UINT dpi, const RECT& rect) {
        HFONT font = makeFont(dpi);
        setChildrenFont(window, font);
        DeleteObject(font_);
        font_ = font;
        dpi_ = dpi;
        SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left,
            rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
    }

    void command(int id) {
        switch (id) {
        case sourceBrowse: if (!busy_) browse(source_, L"选择需要迁移的目录 A"); break;
        case targetBrowse: if (!busy_) browse(target_, L"选择用于存放该目录的文件夹 B"); break;
        case moveButton: if (!busy_) beginMove(); break;
        case historyButton: showHistory(); break;
        case clearButton: SetWindowTextW(output_, L""); break;
        case helpButton: showUsage(); break;
        }
    }

    bool canClose(bool warn = true) const {
        if (busy_ && warn) MessageBoxW(window, L"目录正在迁移，请等待操作完成后再关闭程序。",
            title, MB_OK | MB_ICONWARNING);
        return !busy_;
    }

    void onProgress(const std::wstring& text) {
        SetWindowTextW(status_, text.c_str());
        appendOutput(text);
    }

    void finish(const Result& result) {
        if (worker_.joinable()) worker_.join();
        setBusy(false);
        const auto& record = result.operation;
        appendOutput(record.message);
        std::wstring message = record.message;
        if (!record.backupPath.empty() && !record.removedExistingSource) {
            message += L"\n\n保留的原目录：" + record.backupPath;
        }
        if (!record.copyErrors.empty()) message += L"\n\n完整错误清单可在操作历史中查看。";
        if (!result.logError.empty()) {
            message += L"\n\n操作历史保存失败：" + result.logError;
            appendOutput(L"操作历史保存失败：" + result.logError);
        }
        if (record.status == L"success") message += L"\n\n" + record.paths.source.wstring() + L"\n->\n" + record.paths.target.wstring();
        UINT icon = record.status == L"failed" ? MB_ICONERROR : MB_ICONWARNING;
        if (record.status == L"success" && result.logError.empty()) icon = MB_ICONINFORMATION;
        MessageBoxW(window, crlf(message).c_str(), title, MB_OK | icon);
    }

    void initializeHistory(HWND handle) {
        historyWindow = handle;
        historyDpi_ = GetDpiForWindow(handle);
        if (historyFont_) DeleteObject(historyFont_);
        historyFont_ = makeFont(historyDpi_);
        list_ = control(handle, historyFont_, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL |
            LVS_SHOWSELALWAYS | WS_TABSTOP, historyList, WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        detail_ = control(handle, historyFont_, L"EDIT", L"", ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, 0, WS_EX_CLIENTEDGE);
        SendMessageW(detail_, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        const wchar_t* names[] = {L"时间", L"状态", L"原目录 A", L"实际目录", L"结果"};
        const int widths[] = {156, 136, 220, 220, 340};
        for (int index = 0; index < 5; ++index) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<wchar_t*>(names[index]);
            column.cx = MulDiv(widths[index], historyDpi_, 96);
            ListView_InsertColumn(list_, index, &column);
        }
        populateHistory();
        layoutHistory();
    }

    void layoutHistory() {
        if (!detail_) return;
        RECT rect{};
        GetClientRect(historyWindow, &rect);
        const int width = MulDiv(rect.right, 96, historyDpi_);
        const int height = MulDiv(rect.bottom, 96, historyDpi_);
        const int detailHeight = std::max(120, height / 3);
        place(list_, 12, 12, width - 24, height - detailHeight - 36, historyDpi_);
        place(detail_, 12, height - detailHeight - 12, width - 24, detailHeight, historyDpi_);
        padTextArea(detail_, historyDpi_);
    }

    void changeHistoryDpi(UINT dpi, const RECT& rect) {
        HFONT font = makeFont(dpi);
        setChildrenFont(historyWindow, font);
        DeleteObject(historyFont_);
        historyFont_ = font;
        for (int index = 0; index < 5; ++index) {
            ListView_SetColumnWidth(list_, index, MulDiv(ListView_GetColumnWidth(list_, index), dpi, historyDpi_));
        }
        historyDpi_ = dpi;
        SetWindowPos(historyWindow, nullptr, rect.left, rect.top, rect.right - rect.left,
            rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
        layoutHistory();
    }

    void selectHistory() {
        const int index = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (index >= 0 && static_cast<size_t>(index) < rows_.size()) {
            SetWindowTextW(detail_, crlf(rows_[index].detail).c_str());
        }
    }

    void historyDestroyed() {
        historyWindow = nullptr;
        list_ = nullptr;
        detail_ = nullptr;
    }

private:
    void appendOutput(const std::wstring& text) {
        const auto content = crlf(timestamp().substr(11) + L"  " + text + L"\n");
        const int end = GetWindowTextLengthW(output_);
        SendMessageW(output_, EM_SETSEL, end, end);
        SendMessageW(output_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(content.c_str()));
        SendMessageW(output_, EM_SCROLLCARET, 0, 0);
    }

    void setBusy(bool busy) {
        busy_ = busy;
        for (HWND child : {source_, target_, sourceBrowse_, targetBrowse_, move_}) EnableWindow(child, !busy);
        SendMessageW(progress_, PBM_SETMARQUEE, FALSE, 0);
        LONG_PTR style = GetWindowLongPtrW(progress_, GWL_STYLE);
        SetWindowLongPtrW(progress_, GWL_STYLE, busy ? style | PBS_MARQUEE : style & ~PBS_MARQUEE);
        SetWindowPos(progress_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        SendMessageW(progress_, PBM_SETPOS, 0, 0);
        if (busy) SendMessageW(progress_, PBM_SETMARQUEE, TRUE, 30);
        SetWindowTextW(status_, busy ? L"正在准备" : L"就绪");
        InvalidateRect(progress_, nullptr, TRUE);
    }

    void browse(HWND edit, const wchar_t* heading) {
        IFileOpenDialog* dialog = nullptr;
        const HRESULT created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog));
        if (FAILED(created)) fail(L"无法打开目录选择窗口");
        struct ReleaseDialog { IFileOpenDialog* value; ~ReleaseDialog() { value->Release(); } } release{dialog};
        FILEOPENDIALOGOPTIONS options{};
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        dialog->SetTitle(heading);
        const auto current = windowText(edit);
        IShellItem* initial = nullptr;
        if (!current.empty() && SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&initial)))) {
            dialog->SetFolder(initial);
            initial->Release();
        }
        const HRESULT shown = dialog->Show(window);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
        if (FAILED(shown)) fail(L"目录选择失败");
        IShellItem* selected = nullptr;
        if (FAILED(dialog->GetResult(&selected))) fail(L"无法读取所选目录");
        PWSTR path = nullptr;
        const HRESULT result = selected->GetDisplayName(SIGDN_FILESYSPATH, &path);
        selected->Release();
        if (FAILED(result)) fail(L"所选项目不是文件系统目录");
        SetWindowTextW(edit, path);
        CoTaskMemFree(path);
    }

    void saveImmediate(Operation& record) {
        try { HistoryWriter(logPath_).append(record); }
        catch (const std::exception& error) { record.message += L"\n历史记录保存失败：" + exceptionText(error); }
        appendOutput(record.message);
    }

    void beginMove() {
        Operation record;
        record.paths.source = windowText(source_);
        record.paths.targetRoot = windowText(target_);
        try {
            record.paths = validatePaths(record.paths.source.wstring(), record.paths.targetRoot.wstring(), appPath_);
            if (!pathExists(record.paths.targetRoot)) {
                const auto prompt = L"目标文件夹不存在，是否创建？\n" + record.paths.targetRoot.wstring();
                if (MessageBoxW(window, prompt.c_str(), title, MB_YESNO | MB_ICONQUESTION) != IDYES) {
                    record.status = L"cancelled";
                    record.message = L"用户取消创建目标目录";
                    saveImmediate(record);
                    return;
                }
            }
            appendOutput(L"改名并复制目录：" + record.paths.source.wstring() + L" -> " + record.paths.target.wstring());
            setBusy(true);
            worker_ = std::thread([this, paths = record.paths] { runJob(paths); });
        } catch (const std::exception& error) {
            setBusy(false);
            record.message = exceptionText(error);
            saveImmediate(record);
            MessageBoxW(window, crlf(record.message).c_str(), title, MB_OK | MB_ICONERROR);
        }
    }

    void runJob(const Paths& paths) {
        auto result = std::make_unique<Result>();
        result->operation.paths = paths;
        try {
            HistoryWriter writer(logPath_);
            result->operation = migrate(paths, [this](const std::wstring& message) {
                auto text = std::make_unique<std::wstring>(message);
                if (PostMessageW(window, progressMessage, 0, reinterpret_cast<LPARAM>(text.get()))) text.release();
            });
            try { writer.append(result->operation); }
            catch (const std::exception& error) { result->logError = exceptionText(error); }
        } catch (const std::exception& error) {
            result->operation.message = exceptionText(error);
        }
        if (PostMessageW(window, finishedMessage, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
    }

    void showUsage() {
        appendOutput(L"使用说明\n"
            L"1. 选择需要迁移的目录（原目录 A）。\n"
            L"2. 选择要将该目录放入的文件夹（目标目录 B）。\n\n"
            L"例如：原目录为 D:\\Games，目标文件夹为 E:\\Storage，迁移后目录为 E:\\Storage\\Games。\n"
            L"选择完成后，点击“移动并建立软链接”。\n");
    }

    void showHistory() {
        auto rows = loadHistory(logPath_);
        rows_ = std::move(rows);
        if (historyWindow) {
            populateHistory();
            ShowWindow(historyWindow, SW_RESTORE);
            SetForegroundWindow(historyWindow);
            return;
        }
        RECT parent{};
        GetWindowRect(window, &parent);
        const int width = MulDiv(940, dpi_, 96);
        const int height = MulDiv(560, dpi_, 96);
        HWND handle = CreateWindowExW(WS_EX_CONTROLPARENT, historyClass, L"操作历史",
            WS_OVERLAPPEDWINDOW, parent.left + 24, parent.top + 24, width, height,
            window, nullptr, GetModuleHandleW(nullptr), this);
        if (!handle) failWin32(L"无法创建历史记录窗口");
        ShowWindow(handle, SW_SHOW);
    }

    void populateHistory() {
        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list_);
        for (size_t index = 0; index < rows_.size(); ++index) {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(index);
            item.pszText = rows_[index].columns[0].data();
            const int row = ListView_InsertItem(list_, &item);
            for (int column = 1; column < 5; ++column) {
                ListView_SetItemText(list_, row, column, rows_[index].columns[column].data());
            }
        }
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list_, nullptr, TRUE);
        if (rows_.empty()) SetWindowTextW(detail_, L"暂无操作记录。");
        else ListView_SetItemState(list_, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }

    UINT dpi_ = 96;
    UINT historyDpi_ = 96;
    HFONT font_ = nullptr;
    HFONT historyFont_ = nullptr;
    HWND admin_ = nullptr, sourceLabel_ = nullptr, targetLabel_ = nullptr;
    HWND source_ = nullptr, target_ = nullptr, sourceBrowse_ = nullptr, targetBrowse_ = nullptr;
    HWND move_ = nullptr, history_ = nullptr, clear_ = nullptr, help_ = nullptr;
    HWND status_ = nullptr, progress_ = nullptr, output_ = nullptr;
    HWND list_ = nullptr, detail_ = nullptr;
    fs::path appPath_, logPath_;
    std::vector<HistoryRow> rows_;
    std::thread worker_;
    bool busy_ = false;
};

LRESULT CALLBACK mainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wParam, lParam);
    try {
        switch (message) {
        case WM_CREATE: app->initialize(window); return 0;
        case WM_SIZE: if (wParam != SIZE_MINIMIZED) app->layout(); return 0;
        case WM_DPICHANGED: app->changeDpi(HIWORD(wParam), *reinterpret_cast<RECT*>(lParam)); return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = GetDpiForWindow(window);
            RECT rect{0, 0, MulDiv(620, dpi, 96), MulDiv(370, dpi, 96)};
            AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT, dpi);
            info->ptMinTrackSize = {rect.right - rect.left, rect.bottom - rect.top};
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            const auto brush = app->textAreaColor(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
            if (brush) return brush;
            break;
        }
        case WM_COMMAND: if (HIWORD(wParam) == BN_CLICKED) app->command(LOWORD(wParam)); return 0;
        case WM_SYSCOMMAND: if (wParam == licenseCommand) { showLicense(window); return 0; } break;
        case progressMessage: {
            const std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
            app->onProgress(*text);
            return 0;
        }
        case finishedMessage: {
            const std::unique_ptr<Result> result(reinterpret_cast<Result*>(lParam));
            app->finish(*result);
            return 0;
        }
        case WM_QUERYENDSESSION: return app->canClose(false) ? TRUE : FALSE;
        case WM_CLOSE: if (app->canClose()) DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& error) {
        MessageBoxW(window, exceptionText(error).c_str(), title, MB_OK | MB_ICONERROR);
        if (message == WM_CREATE) return -1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK historyProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wParam, lParam);
    try {
        switch (message) {
        case WM_CREATE: app->initializeHistory(window); return 0;
        case WM_SIZE: if (wParam != SIZE_MINIMIZED) app->layoutHistory(); return 0;
        case WM_DPICHANGED: app->changeHistoryDpi(HIWORD(wParam), *reinterpret_cast<RECT*>(lParam)); return 0;
        case WM_GETMINMAXINFO: {
            const UINT dpi = GetDpiForWindow(window);
            reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {MulDiv(700, dpi, 96), MulDiv(420, dpi, 96)};
            return 0;
        }
        case WM_NOTIFY: {
            const auto* notification = reinterpret_cast<NMHDR*>(lParam);
            if (notification->idFrom == historyList && notification->code == LVN_ITEMCHANGED) app->selectHistory();
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            const auto brush = app->textAreaColor(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam));
            if (brush) return brush;
            break;
        }
        case WM_DESTROY: app->historyDestroyed(); return 0;
        }
    } catch (const std::exception& error) {
        MessageBoxW(window, exceptionText(error).c_str(), title, MB_OK | MB_ICONERROR);
        if (message == WM_CREATE) return -1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void registerWindows(HINSTANCE instance) {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    cls.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_SHARED));
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    cls.lpszClassName = mainClass;
    cls.lpfnWndProc = mainProc;
    if (!RegisterClassExW(&cls)) failWin32(L"无法注册主窗口");
    cls.lpszClassName = historyClass;
    cls.lpfnWndProc = historyProc;
    if (!RegisterClassExW(&cls)) failWin32(L"无法注册历史窗口");
}
}

int run(HINSTANCE instance, int show) {
    if (!administrator()) {
        MessageBoxW(nullptr, L"必须以管理员权限运行，程序将退出。", title, MB_OK | MB_ICONERROR);
        return 1;
    }
    Handle mutex(CreateMutexW(nullptr, FALSE, L"Local\\SoftLinkGuiSingleInstance"));
    const DWORD mutexError = GetLastError();
    if (!mutex) failWin32(L"无法创建单实例锁", mutexError);
    if (mutexError == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(nullptr, title);
        if (existing) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
        return 0;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) fail(L"无法初始化 Windows 目录选择组件");
    struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } com;
    SetCurrentProcessExplicitAppUserModelID(L"ToolHub.SoftLink");
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES};
    if (!InitCommonControlsEx(&common)) failWin32(L"无法初始化 Windows 控件");
    registerWindows(instance);
    const UINT dpi = GetDpiForSystem();
    RECT rect{0, 0, MulDiv(680, dpi, 96), MulDiv(420, dpi, 96)};
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT, dpi);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    App app;
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, mainClass, title, WS_OVERLAPPEDWINDOW,
        work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2,
        width, height, nullptr, nullptr, instance, &app);
    if (!window) failWin32(L"无法创建主窗口");
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (app.historyWindow && IsDialogMessageW(app.historyWindow, &message)) continue;
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try { return softlink::run(instance, show); }
    catch (const std::exception& error) {
        MessageBoxW(nullptr, softlink::exceptionText(error).c_str(), L"SoftLink", MB_OK | MB_ICONERROR);
        return 1;
    }
}
