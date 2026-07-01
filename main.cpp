#include <windows.h>
#include <string>
#include <vector>

class ClipboardGuard {
    bool opened = false;

public:
    explicit ClipboardGuard(HWND hwnd) {
        for (int i = 0; i < 5; ++i) {
            if (OpenClipboard(hwnd)) {
                opened = true;
                break;
            }
            Sleep(20);
        }
    }

    ~ClipboardGuard() { if (opened) CloseClipboard(); }
    bool is_open() const { return opened; }

    ClipboardGuard(const ClipboardGuard &) = delete;

    ClipboardGuard &operator=(const ClipboardGuard &) = delete;
};

class GlobalLockGuard {
    HANDLE hMem;
    void *ptr;

public:
    explicit GlobalLockGuard(HANDLE h) : hMem(h), ptr(GlobalLock(h)) {
    }

    ~GlobalLockGuard() { if (ptr) GlobalUnlock(hMem); }
    void *get() const { return ptr; }

    GlobalLockGuard(const GlobalLockGuard &) = delete;

    GlobalLockGuard &operator=(const GlobalLockGuard &) = delete;
};

class TextProcessor {
    static void replace_all(std::wstring &str, const std::wstring &from, const std::wstring &to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::wstring::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
    }

    static std::wstring remove_quotes_and_brackets(std::wstring text) {
        const std::wstring chars = L"“”\"‘’'《》【】[]";
        std::wstring res;
        res.reserve(text.size());
        for (wchar_t c: text) {
            if (chars.find(c) == std::wstring::npos) res += c;
        }
        return res;
    }

    static std::wstring normalize_punctuation(std::wstring text) {
        replace_all(text, L"……", L"\x0001");
        replace_all(text, L"...", L"\x0001");
        replace_all(text, L"。。。", L"\x0001");

        replace_all(text, L"。", L"，");
        replace_all(text, L"、", L"，");
        replace_all(text, L"；", L"，");
        replace_all(text, L";", L"，");
        replace_all(text, L"——", L"，");

        replace_all(text, L"\x0001", L"...");
        return text;
    }

    static std::wstring replace_words(std::wstring text) {
        const std::vector<std::pair<std::wstring, std::wstring> > words = {
            {L"笔者", L"我"}, {L"因此", L"所以"}, {L"此外", L"而且"},
            {L"例如", L"比如"}, {L"综上所述", L"反正"}
        };
        for (const auto &p: words) {
            replace_all(text, p.first, p.second);
        }
        return text;
    }

    static std::wstring trim_line(const std::wstring &line) {
        const std::wstring drop = L" 。.！!？?,，;；\r\n";
        size_t first = line.find_first_not_of(L" \t");
        if (first == std::wstring::npos) return L"";
        size_t last = line.find_last_not_of(drop);
        return line.substr(first, last - first + 1);
    }

    static std::wstring trim_lines(const std::wstring &text) {
        std::wstring res;
        size_t start = 0, end = 0;
        while ((end = text.find(L'\n', start)) != std::wstring::npos) {
            std::wstring line = trim_line(text.substr(start, end - start));
            if (!line.empty()) res += line + L"\n";
            start = end + 1;
        }
        std::wstring line = trim_line(text.substr(start));
        if (!line.empty()) res += line;
        if (!res.empty() && res.back() == L'\n') res.pop_back();
        return res;
    }

public:
    static std::wstring process(std::wstring text) {
        text = remove_quotes_and_brackets(text);
        text = normalize_punctuation(text);
        text = replace_words(text);
        return trim_lines(text);
    }
};

class ClipFilterApp {
    HWND hMsg = nullptr;
    HWND hOsd = nullptr;
    bool active = false;
    std::wstring last_clip;
    std::wstring osd_text;

    static LRESULT CALLBACK MsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto *app = reinterpret_cast<ClipFilterApp *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            app = static_cast<ClipFilterApp *>(reinterpret_cast<CREATESTRUCT *>(lp)->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        return app ? app->handle_msg(hwnd, msg, wp, lp) : DefWindowProc(hwnd, msg, wp, lp);
    }

    static LRESULT CALLBACK OsdProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto *app = reinterpret_cast<ClipFilterApp *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            app = static_cast<ClipFilterApp *>(reinterpret_cast<CREATESTRUCT *>(lp)->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        return app ? app->handle_osd(hwnd, msg, wp, lp) : DefWindowProc(hwnd, msg, wp, lp);
    }

    LRESULT handle_msg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CLIPBOARDUPDATE:
                process_clipboard();
                return 0;
            case WM_TIMER:
                if (wp == 2) {
                    KillTimer(hwnd, 2);
                    PostQuitMessage(0);
                }
                return 0;
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    LRESULT handle_osd(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc;
                GetClientRect(hwnd, &rc);

                HBRUSH bg = CreateSolidBrush(RGB(36, 36, 36));
                FillRect(hdc, &rc, bg);
                DeleteObject(bg);

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(245, 245, 245));
                HFONT font = CreateFontW(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
                HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));
                DrawTextW(hdc, osd_text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SelectObject(hdc, oldFont);
                DeleteObject(font);
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_TIMER:
                if (wp == 1) {
                    KillTimer(hwnd, 1);
                    ShowWindow(hwnd, SW_HIDE);
                }
                return 0;
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    void show_osd(const std::wstring &text) {
        osd_text = text;
        InvalidateRect(hOsd, nullptr, TRUE);
        ShowWindow(hOsd, SW_SHOWNOACTIVATE);
        SetTimer(hOsd, 1, 1200, nullptr);
    }

    void process_clipboard() {
        if (!active) return;

        std::wstring curr;
        {
            ClipboardGuard cb(hMsg);
            if (!cb.is_open()) return;
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (!hData) return;
            GlobalLockGuard lock(hData);
            if (!lock.get()) return;
            curr = static_cast<wchar_t *>(lock.get());
        }

        if (curr.empty() || curr == last_clip) return;

        std::wstring next = TextProcessor::process(curr);
        if (next == curr) {
            last_clip = curr;
            return;
        }

        {
            ClipboardGuard cb(hMsg);
            if (!cb.is_open()) return;
            EmptyClipboard();
            size_t bytes = (next.length() + 1) * sizeof(wchar_t);
            HANDLE hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem) {
                GlobalLockGuard lock(hMem);
                if (lock.get()) memcpy(lock.get(), next.c_str(), bytes);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        last_clip = next;
    }

public:
    ClipFilterApp() {
        WNDCLASSW wcMsg = {0};
        wcMsg.lpfnWndProc = MsgProc;
        wcMsg.hInstance = GetModuleHandle(nullptr);
        wcMsg.lpszClassName = L"ClipFilterMsgClass";
        RegisterClassW(&wcMsg);
        hMsg = CreateWindowW(wcMsg.lpszClassName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wcMsg.hInstance, this);

        WNDCLASSW wcOsd = {0};
        wcOsd.lpfnWndProc = OsdProc;
        wcOsd.hInstance = GetModuleHandle(nullptr);
        wcOsd.lpszClassName = L"ClipFilterOsdClass";
        RegisterClassW(&wcOsd);

        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        hOsd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
            wcOsd.lpszClassName, nullptr, WS_POPUP,
            (sw - 220) / 2, sh - 45 - 120, 220, 45,
            nullptr, nullptr, wcOsd.hInstance, this
        );
        SetLayeredWindowAttributes(hOsd, 0, 220, LWA_ALPHA);

        RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_ALT, 'T');
        RegisterHotKey(nullptr, 2, MOD_CONTROL | MOD_ALT, 'Q');

        AddClipboardFormatListener(hMsg);
    }

    ~ClipFilterApp() {
        RemoveClipboardFormatListener(hMsg);
    }

    void run() {
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (msg.message == WM_HOTKEY) {
                if (msg.wParam == 1) {
                    active = !active;
                    show_osd(active ? L"剪贴板过滤：开" : L"剪贴板过滤：关");
                } else if (msg.wParam == 2) {
                    active = false;
                    show_osd(L"程序已终止运行");
                    SetTimer(hMsg, 2, 1200, nullptr);
                }
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
};

int main() {
    HWND hWndConsole = GetConsoleWindow();
    if (hWndConsole) ShowWindow(hWndConsole, SW_HIDE);

    ClipFilterApp app;
    app.run();

    return 0;
}
