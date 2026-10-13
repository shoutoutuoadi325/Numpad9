#include "language_bar.h"
#include "identity.h"
#include <algorithm>
#include <cwchar>
#include <vector>
#include <olectl.h>

namespace numpad9::ime {
namespace {
constexpr DWORD kButtonStyle = 0x00010000; // TF_LBI_STYLE_BTN_BUTTON
constexpr DWORD kTrayStyle = 0x00000002; // TF_LBI_STYLE_SHOWNINTRAY
constexpr DWORD kTextColorIcon = 0x00000020; // TF_LBI_STYLE_TEXTCOLORICON
constexpr DWORD kHiddenStatus = 1;
constexpr DWORD kUpdateFields = 7; // TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP
// A white mode glyph with a semitransparent black outline works in both light
// and dark taskbars. The shell owns and disposes the returned, unshared icon.
HICON ModeIcon(Mode mode) {
    HWND focus = GetFocus();
    UINT dpi = focus ? GetDpiForWindow(focus) : 96;
    if (!dpi) dpi = 96;
    const int size = std::clamp(MulDiv(16, static_cast<int>(dpi), 96), 16, 64);
    HDC screen = GetDC(nullptr); if (!screen) return nullptr;
    HDC dc = CreateCompatibleDC(screen); ReleaseDC(nullptr, screen); if (!dc) return nullptr;
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size; info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP color = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!color) { DeleteDC(dc); return nullptr; }
    auto oldBitmap = SelectObject(dc, color);
    auto font = CreateFontW(-size + 2, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                           mode == Mode::Pinyin ? L"Microsoft YaHei UI" : L"Segoe UI");
    auto oldFont = SelectObject(dc, font);
    RECT rect{0, 0, size, size}; FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetTextColor(dc, RGB(255, 255, 255)); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, mode == Mode::Pinyin ? L"中" : L"A", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    SelectObject(dc, oldFont); if (font) DeleteObject(font);
    SelectObject(dc, oldBitmap); DeleteDC(dc);
    auto data = static_cast<DWORD*>(pixels);
    try {
        std::vector<DWORD> glyph(data, data + size * size);
        for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
            auto coverage = static_cast<unsigned>(glyph[y * size + x] & 0xff);
            unsigned outline = 0;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                if (x + dx >= 0 && x + dx < size && y + dy >= 0 && y + dy < size)
                    outline = std::max(outline, static_cast<unsigned>(glyph[(y + dy) * size + x + dx] & 0xff));
            const unsigned alpha = coverage + outline / 2 * (255 - coverage) / 255;
            data[y * size + x] = (alpha << 24) | (coverage << 16) | (coverage << 8) | coverage;
        }
        const int stride = (size + 15) / 16 * 2;
        std::vector<BYTE> bits(static_cast<size_t>(stride * size), 0);
        HBITMAP mask = CreateBitmap(size, size, 1, 1, bits.data());
        if (!mask) { DeleteObject(color); return nullptr; }
        ICONINFO icon{}; icon.fIcon = TRUE; icon.hbmColor = color; icon.hbmMask = mask;
        HICON result = CreateIconIndirect(&icon); DeleteObject(mask); DeleteObject(color); return result;
    } catch (...) { DeleteObject(color); return nullptr; }
}
}
HRESULT ModeButton::QueryInterface(REFIID iid, void** result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfLangBarItem || iid == kIidLangBarButton) *result = static_cast<ITfLangBarItemButton*>(this);
    else if (iid == IID_ITfSource) *result = static_cast<ITfSource*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
}
void ModeButton::SetMode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    ComPtr<ITfLangBarItemSink> sink(sink_.get());
    if (sink) sink->OnUpdate(kUpdateFields);
}
HRESULT ModeButton::GetInfo(TF_LANGBARITEMINFO* info) {
    if (!info) return E_POINTER;
    *info = {}; info->clsidService = kClsid; info->guidItem = kInputModeItem;
    info->dwStyle = kButtonStyle | kTrayStyle | kTextColorIcon;
    const wchar_t description[] = L"Numpad9 拼音 / English";
    std::copy(std::begin(description), std::end(description), info->szDescription);
    return S_OK;
}
HRESULT ModeButton::GetStatus(DWORD* status) {
    if (!status) return E_POINTER;
    *status = (shown_ ? 0 : kHiddenStatus) | (toggle_ ? 0 : 2); return S_OK;
}
HRESULT ModeButton::Show(BOOL show) {
    shown_ = show != FALSE;
    ComPtr<ITfLangBarItemSink> sink(sink_.get()); if (sink) sink->OnUpdate(0x10000); return S_OK;
}
HRESULT ModeButton::GetTooltipString(BSTR* tooltip) {
    if (!tooltip) return E_POINTER;
    *tooltip = SysAllocString(mode_ == Mode::Pinyin ? L"Numpad9 九键拼音 · 点击切换 English · Ctrl+Shift+E" : L"Numpad9 T9 English · 点击切换拼音 · Ctrl+Shift+E");
    return *tooltip ? S_OK : E_OUTOFMEMORY;
}
HRESULT ModeButton::OnClick(TfLBIClick click, POINT, const RECT*) {
    if (click != TF_LBI_CLK_LEFT) return S_FALSE;
    try { return toggle_ ? toggle_() : E_UNEXPECTED; } catch (...) { return E_FAIL; }
}
HRESULT ModeButton::GetIcon(HICON* icon) {
    if (!icon) return E_POINTER;
    *icon = ModeIcon(mode_); return *icon ? S_OK : E_OUTOFMEMORY;
}
HRESULT ModeButton::GetText(BSTR* text) {
    if (!text) return E_POINTER;
    *text = SysAllocString(mode_ == Mode::Pinyin ? L"中" : L"A"); return *text ? S_OK : E_OUTOFMEMORY;
}
HRESULT ModeButton::AdviseSink(REFIID iid, IUnknown* unknown, DWORD* cookie) {
    if (!unknown || !cookie) return E_POINTER;
    *cookie = 0xffffffff;
    if (iid != IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    const auto hr = unknown->QueryInterface(IID_ITfLangBarItemSink, reinterpret_cast<void**>(sink_.put()));
    if (SUCCEEDED(hr)) *cookie = 1;
    return hr;
}
HRESULT ModeButton::UnadviseSink(DWORD cookie) {
    if (cookie != 1 || !sink_) return CONNECT_E_NOCONNECTION;
    sink_.reset(); return S_OK;
}
}
