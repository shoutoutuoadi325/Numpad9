#include "candidate_window.h"
#include "com.h"
#include <algorithm>
#include <mutex>
#include <shobjidl.h>

namespace numpad9::ime {
namespace {
constexpr wchar_t kWindowClass[] = L"Numpad9.CandidateWindow.v1";
std::mutex classMutex;
size_t classUsers = 0;
UINT WindowDpi(HWND hwnd) {
    const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
    return dpi ? dpi : 96;
}
}
CandidateWindow::~CandidateWindow() { Destroy(); }
void CandidateWindow::Hide() {
    accessibility_.Hide();
    if (window_ && visible_) {
        ShowWindow(window_, SW_HIDE);
        NotifyWinEvent(EVENT_OBJECT_IME_HIDE, window_, OBJID_CLIENT, CHILDID_SELF);
    }
    visible_ = false;
}
void CandidateWindow::Destroy() {
    Hide(); accessibility_.Detach();
    if (window_) {
        std::lock_guard<std::mutex> guard(classMutex);
        DestroyWindow(window_); window_ = nullptr;
        if (classUsers) --classUsers;
        if (!classUsers) UnregisterClassW(kWindowClass, gModule);
    }
    if (font_) { DeleteObject(font_); font_ = nullptr; }
}
void CandidateWindow::Update(const RECT& caret, HWND owner, Mode mode, const std::string& digits,
                             const std::vector<Candidate>& candidates, size_t selected, bool dictionaryReady) {
    if (!window_) {
        std::lock_guard<std::mutex> guard(classMutex);
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc; wc.hInstance = gModule; wc.lpszClassName = kWindowClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!classUsers && !RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                 kWindowClass, L"Numpad9 候选词", WS_POPUP | WS_BORDER,
                                 0, 0, 0, 0, owner, nullptr, gModule, this);
        if (!window_) { if (!classUsers) UnregisterClassW(kWindowClass, gModule); return; }
        ++classUsers;
        accessibility_.Attach(window_);
    }
    if (!owner) owner = GetFocus();
    // Contexts in the same app can have different owning windows.
    SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    auto newDpi = WindowDpi(owner);
    if (newDpi != dpi_ || !font_) {
        dpi_ = newDpi;
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-scale(17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Microsoft YaHei UI");
    }
    title_ = mode == Mode::Pinyin ? L"九键拼音  " : L"T9 English  ";
    title_.append(digits.begin(), digits.end());
    dictionaryReady_ = dictionaryReady;
    candidates_ = candidates; selected_ = selected;
    const size_t page = selected_ / 9;
    const size_t pageBegin = page * 9;
    const size_t rows = candidates_.empty() ? 1 : std::min<size_t>(9, candidates_.size() - pageBegin);
    const int width = scale(450), height = scale(46 + static_cast<int>(rows) * 31 + 42);
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST), &monitor);
    int x = std::clamp(static_cast<int>(caret.left), static_cast<int>(monitor.rcWork.left),
                       std::max(static_cast<int>(monitor.rcWork.left), static_cast<int>(monitor.rcWork.right) - width));
    int y = caret.bottom + scale(4);
    if (y + height > monitor.rcWork.bottom) y = caret.top - height - scale(4);
    y = std::max(y, static_cast<int>(monitor.rcWork.top));
    // An already-visible touch keyboard can overlap the hardware-keypad UI.
    // Query the framework's actual occlusion rectangle rather than guessing its size.
    ComPtr<IFrameworkInputPane> inputPane;
    constexpr GUID paneClass = {0xd5120aa3, 0x46ba, 0x44c5, {0x82, 0x2d, 0xca, 0x80, 0x92, 0xc1, 0xfc, 0x72}};
    constexpr GUID paneInterface = {0x5752238b, 0x24f0, 0x495a, {0x82, 0xf1, 0x2f, 0xd5, 0x93, 0x05, 0x67, 0x96}};
    if (SUCCEEDED(CoCreateInstance(paneClass, nullptr, CLSCTX_INPROC_SERVER, paneInterface, reinterpret_cast<void**>(inputPane.put())))) {
        RECT keyboard{}, proposed{x, y, x + width, y + height}, overlap{};
        if (SUCCEEDED(inputPane->Location(&keyboard)) && IntersectRect(&overlap, &keyboard, &proposed))
            y = std::max(static_cast<int>(monitor.rcWork.top), static_cast<int>(keyboard.top) - height - scale(4));
    }
    SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    NotifyWinEvent(visible_ ? EVENT_OBJECT_IME_CHANGE : EVENT_OBJECT_IME_SHOW, window_, OBJID_CLIENT, CHILDID_SELF);
    visible_ = true;
    accessibility_.Update(candidates_, selected_, dpi_, true);
    InvalidateRect(window_, nullptr, TRUE);
    // Notify accessibility clients that the visible window name/text changed.
    SetWindowTextW(window_, title_.c_str());
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, window_, OBJID_WINDOW, CHILDID_SELF);
}
LRESULT CALLBACK CandidateWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<CandidateWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        if (message == WM_PAINT) { self->Paint(); return 0; }
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_GETOBJECT) return self->accessibility_.GetObject(wParam, lParam);
        if (message == kCandidateActionMessage) {
            if (self->accessibility_.IsCurrentGeneration(lParam) && self->action_) {
                const size_t index = wParam & 0xffffu;
                if (index < self->candidates_.size()) self->action_(index, (wParam & 0x10000u) != 0);
            }
            return 0;
        }
        if (message == WM_LBUTTONDOWN) {
            const int y = static_cast<short>((lParam >> 16) & 0xffff);
            const int row = (y - self->scale(44)) / self->scale(31);
            const size_t index = self->selected_ / 9 * 9 + static_cast<size_t>(std::max(row, 0));
            if (y >= self->scale(44) && row >= 0 && row < 9 && index < self->candidates_.size() && self->action_) self->action_(index, true);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
void CandidateWindow::Paint() {
    PAINTSTRUCT ps{}; HDC dc = BeginPaint(window_, &ps);
    RECT client{}; GetClientRect(window_, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
    auto oldFont = SelectObject(dc, font_); SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    RECT title{scale(13), scale(10), client.right - scale(13), scale(38)};
    DrawTextW(dc, title_.c_str(), -1, &title, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    size_t begin = selected_ / 9 * 9;
    for (size_t i = begin; i < std::min(begin + 9, candidates_.size()); ++i) {
        RECT row{scale(8), scale(44 + static_cast<int>(i - begin) * 31), client.right - scale(8), scale(74 + static_cast<int>(i - begin) * 31)};
        if (i == selected_) { FillRect(dc, &row, GetSysColorBrush(COLOR_HIGHLIGHT)); SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT)); }
        else SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        std::wstring text = std::to_wstring(i - begin + 1) + L".  " + candidates_[i].text;
        text += L"    "; text.append(candidates_[i].spelling.begin(), candidates_[i].spelling.end());
        row.left += scale(7);
        DrawTextW(dc, text.c_str(), -1, &row, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (candidates_.empty()) {
        RECT row{scale(14), scale(44), client.right - scale(12), scale(74)};
        auto text = dictionaryReady_ ? L"暂无候选词 · Enter 保留数字" : L"词库不可用 · 请修复安装";
        DrawTextW(dc, text, -1, &row, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    RECT footer{scale(13), client.bottom - scale(35), client.right - scale(13), client.bottom - scale(7)};
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    std::wstring help = L"↑↓ / * 选词 · +− 翻页 · 空格 确认 · Esc 取消";
    DrawTextW(dc, help.c_str(), -1, &footer, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, oldFont); EndPaint(window_, &ps);
}
}
