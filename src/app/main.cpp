#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <msctf.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "identity.h"
#ifdef NUMPAD9_MSIX
#include <roapi.h>
#include <winrt/Windows.Data.Json.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <memory>
#include <atomic>
#include "input_service.h"
#include "candidate_window.h"
namespace numpad9::ime {
HINSTANCE gModule = nullptr;
std::atomic<long> gObjects{0}, gServerLocks{0};
}
#endif

// Older MinGW headers omit these flags; values are from Microsoft's msctf.h.
#ifndef TF_IPPMF_FORSESSION
#define TF_IPPMF_FORSESSION 0x20000000
#define TF_IPPMF_ENABLEPROFILE 0x00000001
#endif

namespace {
constexpr int kActivate = 101, kSettings = 102, kHelp = 103, kPrivacy = 104,
              kAbout = 105, kLanguage = 106, kClear = 107, kPractice = 108;
HINSTANCE instance;
HWND mainWindow, practice, status;
HFONT bodyFont, titleFont, smallFont, keyFont;
UINT dpi = 96;
int scrollY = 0, contentHeight = 0;
bool english = false;
std::vector<HWND> controls;
std::vector<HWND> keys;
HWND heading, subtitle, mapTitle, mapNote, practiceTitle, practiceLabel, example, tips, footer;
#ifdef NUMPAD9_MSIX
constexpr UINT kInputChanged = WM_APP + 1, kTrayMessage = WM_APP + 2;
constexpr UINT kTrayOpen = 401, kTrayQuit = 402, kTrayMode = 403;
std::unique_ptr<numpad9::msix::InputService> inputService;
numpad9::ime::CandidateWindow candidateWindow;
bool shuttingDown = false, serviceStarted = false, trayAdded = false;
std::filesystem::path AppDirectory() {
    std::wstring path(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("App path unavailable");
    path.resize(length); return std::filesystem::path(path).parent_path();
}
std::string PathUtf8(const std::filesystem::path& path) {
    return path.u8string();
}
numpad9::msix::Config ReadInputConfig() {
    const auto file = AppDirectory() / L"input-access.json";
    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    std::string text(65537, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(input.gcount()));
    if (text.size() > 65536) throw std::runtime_error("Input configuration too large");
    auto json = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(text));
    auto value = [&json](const wchar_t* key) { return std::wstring(json.GetNamedString(key, L"")); };
    return {value(L"featureId"), value(L"token"), value(L"attestation"), value(L"inputProfile")};
}
void InputChanged(const numpad9::msix::Snapshot& snapshot) {
    if (shuttingDown || !mainWindow) return;
    if (snapshot.active && snapshot.hasFocus && !snapshot.digits.empty()) {
        candidateWindow.SetAction([revision = snapshot.revision](size_t index, bool commit) {
            if (inputService && !shuttingDown) inputService->SelectCandidate(index, revision, commit);
        });
        RECT anchor = snapshot.anchor;
        if (!snapshot.anchorValid) {
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY), &monitor);
            anchor = {monitor.rcWork.left + 24, monitor.rcWork.top + 24,
                      monitor.rcWork.left + 24, monitor.rcWork.top + 24};
        }
        candidateWindow.Update(anchor, nullptr, snapshot.mode, snapshot.digits,
                               snapshot.candidates, snapshot.selected, true);
    } else candidateWindow.Hide();
    PostMessageW(mainWindow, kInputChanged, 0, 0);
}
void AddTray(HWND window) {
    NOTIFYICONDATAW icon{}; icon.cbSize = sizeof(icon); icon.hWnd = window; icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = kTrayMessage; icon.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wcscpy_s(icon.szTip, L"Numpad9 九键输入 / Nine-key input");
    trayAdded = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}
void RemoveTray(HWND window) {
    NOTIFYICONDATAW icon{}; icon.cbSize = sizeof(icon); icon.hWnd = window; icon.uID = 1;
    if (trayAdded) Shell_NotifyIconW(NIM_DELETE, &icon);
    trayAdded = false;
}
#endif
int Scale(int value) { return MulDiv(value, dpi, 96); }
const wchar_t* T(const wchar_t* zh, const wchar_t* en) { return english ? en : zh; }

HWND Control(const wchar_t* type, const wchar_t* text, DWORD style, int id = 0) {
    if (std::wstring(type) == L"BUTTON") style |= BS_NOTIFY;
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 1, 1, mainWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
    controls.push_back(control);
    return control;
}
void Fonts() {
    for (HFONT font : {bodyFont, titleFont, smallFont, keyFont}) if (font) DeleteObject(font);
    auto make = [](int height, int weight) {
        return CreateFontW(-Scale(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, L"Segoe UI");
    };
    bodyFont = make(15, FW_NORMAL); titleFont = make(30, FW_SEMIBOLD);
    smallFont = make(13, FW_NORMAL); keyFont = make(19, FW_SEMIBOLD);
    for (HWND control : controls) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
    if (heading) SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
    for (HWND key : keys) SendMessageW(key, WM_SETFONT, reinterpret_cast<WPARAM>(keyFont), TRUE);
    for (HWND control : {mapNote, example, tips, footer}) if (control)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
}
bool Registered() {
#ifdef NUMPAD9_MSIX
    return inputService && inputService->GetSnapshot().active;
#else
    ITfInputProcessorProfiles* profiles = nullptr;
    HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, reinterpret_cast<void**>(&profiles));
    if (FAILED(result)) return false;
    BSTR description = nullptr;
    result = profiles->GetLanguageProfileDescription(numpad9::ime::kClsid,
        numpad9::ime::kLanguage, numpad9::ime::kProfile, &description);
    if (description) SysFreeString(description);
    profiles->Release();
    return SUCCEEDED(result);
#endif
}
void UpdateText() {
    SetWindowTextW(mainWindow, T(L"Numpad9 · 九键输入", L"Numpad9 · Nine-key input"));
    SetWindowTextW(heading, T(L"熟悉的九键，回到电脑。", L"Your familiar nine keys."));
    SetWindowTextW(subtitle, T(L"用小键盘的位置输入拼音与英文。按照手机布局，所有转换都在本地完成。",
        L"Type Pinyin and English by keypad position. Phone layout. Entirely offline."));
    SetWindowTextW(mapTitle, T(L"01  按位置记忆", L"01  Remember positions"));
    SetWindowTextW(mapNote, T(L"大号数字是手机键位；下方标注电脑键帽。\r\n顶部电脑 7 / 8 / 9 → 手机 1 / 2 / 3。",
        L"Large digits show phone keys; PC keycaps appear below.\r\nPC top row 7 / 8 / 9 becomes phone 1 / 2 / 3."));
    const wchar_t* letter[] = {L"， / ,", L"ABC", L"DEF", L"GHI", L"JKL", L"MNO", L"PQRS", L"TUV", L"WXYZ"};
    const int physical[] = {7,8,9,4,5,6,1,2,3};
    for (int i=0; i<9; ++i) {
        std::wstring text = std::to_wstring(i+1)+L"\r\n"+letter[i]+L"\r\nPC "+std::to_wstring(physical[i]);
        SetWindowTextW(keys[i], text.c_str());
    }
    SetWindowTextW(practiceTitle, T(L"02  启用并试一试", L"02  Switch and try"));
    SetWindowTextW(practiceLabel, T(L"练习输入(&T)", L"&Practice input"));
    SetWindowTextW(GetDlgItem(mainWindow,kActivate), T(L"切换到 Numpad9", L"Switch to Numpad9"));
    SetWindowTextW(GetDlgItem(mainWindow,kSettings), T(L"Windows 语言设置", L"Windows language settings"));
    SetWindowTextW(GetDlgItem(mainWindow,kClear), T(L"清空练习内容", L"Clear practice text"));
    SetWindowTextW(GetDlgItem(mainWindow,kHelp), T(L"使用说明", L"Keyboard guide"));
    SetWindowTextW(GetDlgItem(mainWindow,kPrivacy), T(L"隐私政策", L"Privacy policy"));
    SetWindowTextW(GetDlgItem(mainWindow,kAbout), T(L"关于 / 开源许可", L"About / licenses"));
    SetWindowTextW(GetDlgItem(mainWindow,kLanguage), english ? L"中文" : L"English");
    SetWindowTextW(example, T(L"试输入「你好」：按电脑小键盘 6 4 4 8 6，再按空格。\r\n可通过 Win + 空格切换输入法。此文本框使用真正的系统输入服务。",
        L"Try 你好: press PC numpad 6 4 4 8 6, then Space.\r\nWin + Space switches input methods. This field uses the actual Windows text service."));
    SetWindowTextW(tips, T(L"空格 / Enter 确认  ·  Esc 取消  ·  Backspace 删除\r\n小键盘 / 切换中英文  ·  + / − 翻页  ·  * 下一个候选\r\nNum Lock 开关均可用；主键盘保持正常。请在普通文本框中使用。",
        L"Space / Enter: commit · Esc: cancel · Backspace: erase\r\nNumpad /: Chinese / English · + / −: pages · *: next candidate\r\nWorks with Num Lock on or off. Use ordinary text fields."));
    SetWindowTextW(footer, T(L"离线词库 · 不记录输入 · 无广告\r\n关闭此窗口后仍可使用输入法；切回其他输入法即可停止九键转换。",
        L"Offline dictionary · No typing history · No ads\r\nClosing this window keeps the IME available. Switch IMEs to stop conversion."));
#ifdef NUMPAD9_MSIX
    SetWindowTextW(GetDlgItem(mainWindow,kActivate), serviceStarted
        ? T(L"停用九键输入", L"Stop nine-key input") : T(L"启用九键输入", L"Enable nine-key input"));
    SetWindowTextW(example, T(L"试输入「你好」：按电脑小键盘 6 4 4 8 6，再按空格。\r\n启用后在 Windows 输入法菜单中选择 Numpad9。",
        L"Try 你好: press PC numpad 6 4 4 8 6, then Space.\r\nAfter enabling, select Numpad9 in the Windows input-method menu."));
    SetWindowTextW(footer, T(L"离线词库 · 不记录输入 · 无广告\r\n关闭窗口可保留托盘中的输入服务；可随时停用或退出。",
        L"Offline dictionary · No typing history · No ads\r\nClose to keep input running in the tray. Stop or exit at any time."));
    if (inputService) {
        auto snapshot = inputService->GetSnapshot();
        std::wstring text = snapshot.message;
        if (!serviceStarted && snapshot.state == numpad9::msix::State::Stopped)
            text = T(L"点击启用九键输入，然后在 Windows 输入法菜单中选择 Numpad9。",
                     L"Enable nine-key input, then select Numpad9 in the Windows input-method menu.");
        SetWindowTextW(status, text.c_str());
    }
#else
    SetWindowTextW(status, Registered()
        ? T(L"输入服务已安装。点击切换，或使用 Win + 空格选择 Numpad9。", L"Input service installed. Switch below or select Numpad9 with Win + Space.")
        : T(L"未检测到输入服务。请通过完整安装包安装；重新登录后再试。", L"Input service not detected. Use the full installer, then sign out and back in."));
#endif
}
void Place(HWND control, int x, int y, int w, int h) {
    MoveWindow(control, Scale(x), Scale(y)-scrollY, Scale(w), Scale(h), TRUE);
}
void Layout() {
    RECT client{}; GetClientRect(mainWindow,&client);
    int width = MulDiv(client.right,96,dpi);
    bool columns = width >= 850;
    int margin=28, left=margin, mapWidth = columns ? 330 : width-2*margin;
    int right = columns ? 400 : margin, rightWidth = columns ? width-right-margin : width-2*margin;
    int practiceY = columns ? 170 : 630;
    contentHeight=Scale(columns ? 740 : 1170);
    int maxScroll = (contentHeight>client.bottom) ? contentHeight-client.bottom : 0;
    if (scrollY>maxScroll) scrollY=maxScroll;
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,contentHeight,static_cast<UINT>(client.bottom),scrollY,0};
    SetScrollInfo(mainWindow,SB_VERT,&info,TRUE);
    Place(heading,left,28,width-170,45); Place(GetDlgItem(mainWindow,kLanguage),width-118,32,90,32);
    Place(subtitle,left,84,width-2*margin,54);
    Place(mapTitle,left,170,mapWidth,28);
    for (int i=0; i<9; ++i) Place(keys[i],left+(i%3)*110,215+(i/3)*104,100,94);
    Place(mapNote,left,540,mapWidth,80);
    Place(practiceTitle,right,practiceY,rightWidth,28);
    Place(status,right,practiceY+40,rightWidth,52);
    Place(GetDlgItem(mainWindow,kActivate),right,practiceY+104,rightWidth,42);
    Place(GetDlgItem(mainWindow,kSettings),right,practiceY+158,rightWidth,34);
    Place(practiceLabel,right,practiceY+198,rightWidth,22);
    Place(practice,right,practiceY+222,rightWidth,115);
    Place(GetDlgItem(mainWindow,kClear),right,practiceY+345,rightWidth,32);
    Place(example,right,practiceY+390,rightWidth,65);
    Place(tips,right,practiceY+469,rightWidth,77);
    int bottom=columns ? 636 : 1034;
    Place(footer,left,bottom,width-2*margin,55);
    int buttonWidth=(width-2*margin-20)/3;
    Place(GetDlgItem(mainWindow,kHelp),left,bottom+62,buttonWidth,34);
    Place(GetDlgItem(mainWindow,kPrivacy),left+buttonWidth+10,bottom+62,buttonWidth,34);
    Place(GetDlgItem(mainWindow,kAbout),left+2*(buttonWidth+10),bottom+62,buttonWidth,34);
    InvalidateRect(mainWindow,nullptr,TRUE);
}
void RevealFocusedControl(HWND control) {
    if (!control) return;
    RECT bounds{}, client{};
    GetWindowRect(control, &bounds); GetClientRect(mainWindow, &client);
    MapWindowPoints(nullptr, mainWindow, reinterpret_cast<POINT*>(&bounds), 2);
    if (bounds.top < Scale(12)) scrollY += bounds.top - Scale(12);
    else if (bounds.bottom > client.bottom - Scale(12)) scrollY += bounds.bottom - client.bottom + Scale(12);
    else return;
    if (scrollY < 0) scrollY = 0;
    Layout();
}
void Activate() {
#ifdef NUMPAD9_MSIX
    if (!inputService) return;
    if (serviceStarted) { inputService->Stop(); serviceStarted = false; }
    else {
        try { serviceStarted = inputService->Start(ReadInputConfig(), PathUtf8(AppDirectory() / L"lexicon.tsv")); }
        catch (...) { MessageBoxW(mainWindow, T(L"无法读取输入服务配置。请更新应用或联系支持。", L"Input service configuration could not be loaded. Update the app or contact support."), L"Numpad9", MB_OK | MB_ICONINFORMATION); }
    }
    UpdateText(); if (serviceStarted) SetFocus(practice);
#else
    ITfInputProcessorProfileMgr* manager=nullptr;
    HRESULT result=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,reinterpret_cast<void**>(&manager));
    if (SUCCEEDED(result)) {
        result=manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR,numpad9::ime::kLanguage,
            numpad9::ime::kClsid,numpad9::ime::kProfile,nullptr,TF_IPPMF_FORSESSION|TF_IPPMF_ENABLEPROFILE);
        manager->Release();
    }
    if (result!=S_OK) {
        MessageBoxW(mainWindow,T(L"无法切换到 Numpad9。请确认已安装完整安装包，并在 Windows 语言设置中添加「中文（简体，中国）」。新安装后可能需要重新登录。然后使用 Win + 空格选择 Numpad9。",
            L"Could not switch to Numpad9. Install the full package and add Chinese (Simplified, China) in Windows language settings. You may need to sign out after installation. Then select Numpad9 with Win + Space."),L"Numpad9",MB_OK|MB_ICONINFORMATION);
    } else { SetFocus(practice); }
    UpdateText();
#endif
}
void CreateControls() {
    heading=Control(L"STATIC",L"",0); subtitle=Control(L"STATIC",L"",0);
    Control(L"BUTTON",L"English",WS_TABSTOP,kLanguage);
    mapTitle=Control(L"STATIC",L"",0);
    for (int i=0;i<9;++i) keys.push_back(Control(L"STATIC",L"",SS_CENTER|WS_BORDER));
    mapNote=Control(L"STATIC",L"",0); practiceTitle=Control(L"STATIC",L"",0);
    status=Control(L"STATIC",L"",SS_NOPREFIX);
    Control(L"BUTTON",L"",WS_TABSTOP|BS_DEFPUSHBUTTON,kActivate);
    Control(L"BUTTON",L"",WS_TABSTOP,kSettings);
    practiceLabel=Control(L"STATIC",L"",0);
    practice=Control(MSFTEDIT_CLASS,L"",WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,kPractice);
    SendMessageW(practice,EM_SETLIMITTEXT,100000,0);
    SendMessageW(practice,EM_SETEDITSTYLE,SES_USECTF,SES_USECTF);
    Control(L"BUTTON",L"",WS_TABSTOP,kClear);
    example=Control(L"STATIC",L"",0); tips=Control(L"STATIC",L"",0); footer=Control(L"STATIC",L"",0);
    Control(L"BUTTON",L"",WS_TABSTOP,kHelp); Control(L"BUTTON",L"",WS_TABSTOP,kPrivacy);
    Control(L"BUTTON",L"",WS_TABSTOP,kAbout);
    Fonts(); UpdateText(); Layout();
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch(message) {
    case WM_CREATE:
        mainWindow=window; dpi=GetDpiForWindow(window);
#ifdef NUMPAD9_MSIX
        inputService = std::make_unique<numpad9::msix::InputService>(InputChanged);
        AddTray(window);
#endif
        CreateControls(); return 0;
#ifdef NUMPAD9_MSIX
    case kInputChanged: UpdateText(); return 0;
    case kTrayMessage:
        if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); }
        else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kTrayOpen, T(L"打开 Numpad9", L"Open Numpad9"));
            AppendMenuW(menu, MF_STRING, kTrayMode, T(L"切换拼音 / English", L"Switch Pinyin / English"));
            AppendMenuW(menu, MF_STRING, kTrayQuit, T(L"退出", L"Exit"));
            POINT cursor{}; GetCursorPos(&cursor); SetForegroundWindow(window);
            UINT action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
            DestroyMenu(menu);
            if (action == kTrayOpen) { ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); }
            else if (action == kTrayMode && inputService) inputService->ToggleMode();
            else if (action == kTrayQuit) DestroyWindow(window);
            PostMessageW(window, WM_NULL, 0, 0);
        }
        return 0;
    case WM_CLOSE:
        if (serviceStarted && trayAdded) { ShowWindow(window, SW_HIDE); return 0; }
        DestroyWindow(window); return 0;
#endif
    case WM_SIZE: Layout(); return 0;
    case WM_DPICHANGED: {
        dpi=HIWORD(wParam); auto rect=reinterpret_cast<RECT*>(lParam);
        Fonts(); SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE); Layout(); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info=reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize={Scale(610),Scale(390)};
        // Permit a full-size help window on a smaller remote desktop as well.
        info->ptMaxTrackSize={Scale(4096),Scale(4096)}; return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO info{}; info.cbSize=sizeof(info); info.fMask=SIF_ALL;
        GetScrollInfo(window,SB_VERT,&info);
        switch(LOWORD(wParam)) {
        case SB_LINEUP: scrollY-=Scale(30); break; case SB_LINEDOWN: scrollY+=Scale(30); break;
        case SB_PAGEUP: scrollY-=static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: scrollY+=static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK: scrollY=info.nTrackPos; break;
        default: return 0;
        }
        if(scrollY<0) scrollY=0;
        Layout(); return 0;
    }
    case WM_MOUSEWHEEL:
        scrollY-=GET_WHEEL_DELTA_WPARAM(wParam)*Scale(60)/WHEEL_DELTA;
        if(scrollY<0) scrollY=0;
        Layout(); return 0;
    case WM_COMMAND:
        if (HIWORD(wParam)==BN_SETFOCUS || HIWORD(wParam)==EN_SETFOCUS) {
            RevealFocusedControl(reinterpret_cast<HWND>(lParam)); return 0;
        }
        if (HIWORD(wParam)!=BN_CLICKED) break;
        switch(LOWORD(wParam)) {
        case kActivate: Activate(); return 0;
        case kLanguage: english=!english; UpdateText(); return 0;
        case kSettings: ShellExecuteW(window,L"open",L"ms-settings:regionlanguage",nullptr,nullptr,SW_SHOWNORMAL); return 0;
        case kClear: SetWindowTextW(practice,L""); SetFocus(practice); return 0;
        case kHelp:
#ifdef NUMPAD9_MSIX
            MessageBoxW(window,T(L"1. 点击「启用九键输入」，再在 Windows 输入法菜单中选择 Numpad9。应用需要 Windows 11 24H2 或更新版本。\n2. 按手机键位的位置输入：nihao → 手机 64426 → 电脑小键盘 6 4 4 8 6。\n3. 空格 / Enter 确认；方向键 / * 选择；+ / − 翻页；Esc 取消；Backspace 删除。\n4. / 或 Ctrl + Shift + E 切换拼音与英文；电脑 7 输入逗号；小数点输入句号；0 确认或输入空格。\n5. 输入服务仅处理当前普通文本框。密码、PIN、私密和受保护字段不进行转换。\n\n关闭窗口时输入服务可留在托盘中。点击「停用九键输入」可停止转换；右键托盘选择「退出」可关闭应用。",
                L"1. Enable nine-key input, then select Numpad9 in the Windows input-method menu. Windows 11 24H2 or later is required.\n2. Follow phone key positions: nihao -> phone 64426 -> PC numpad 6 4 4 8 6.\n3. Space / Enter commits; arrows / * select; + / - pages; Esc cancels; Backspace deletes.\n4. / or Ctrl + Shift + E switches Pinyin / English; PC 7 inserts a comma; decimal inserts a full stop; 0 commits or inserts a space.\n5. Use ordinary text fields. Passwords, PINs, private and protected fields are excluded.\n\nClose the window to keep input running in the tray. Stop nine-key input disables conversion; Exit in the tray menu closes the app."),T(L"使用说明",L"Keyboard guide"),MB_OK); return 0;
#else
            MessageBoxW(window,T(L"1. 安装后，点击「切换到 Numpad9」或按 Win + 空格选择 Numpad9。\n2. 看手机键位的位置，忽略电脑键帽数字。中文输入不带声调的完整拼音：nihao → 64426；按电脑小键盘 6 4 4 8 6。\n3. 空格 / Enter 确认候选；方向键 / 小键盘 * 选择；小键盘 + / − 翻页；Esc 取消；Backspace 删除。\n4. 小键盘 / 或 Ctrl + Shift + E 切换中英文；手机 1（电脑 7）输入逗号；小键盘小数点输入句号；小键盘 0 确认候选或输入空格。\n5. 可逐词确认组合成句。不支持云预测或个人词库学习。切回其他输入法即可正常使用小键盘。\n\n输入法在当前文本应用内工作；不适用于密码、安全桌面或不支持 TSF 的应用。", 
                L"1. Click Switch to Numpad9, or choose Numpad9 with Win + Space.\n2. Use phone key positions, ignoring PC keycap numbers. For Pinyin nihao (logical 64426), press PC numpad 6 4 4 8 6.\n3. Space / Enter commits; arrows / numpad * select; + / - pages; Esc cancels; Backspace deletes.\n4. Numpad / or Ctrl + Shift + E switches Chinese / English. Phone 1 (PC 7) inserts a comma; keypad decimal inserts a full stop; numpad 0 commits or inserts a space.\n5. Commit words to compose sentences. Cloud prediction and personal learning are not provided. Switch IMEs to restore numeric typing.\n\nUse ordinary TSF-compatible text fields. Passwords and secure desktops are excluded."),T(L"使用说明",L"Keyboard guide"),MB_OK); return 0;
#endif
        case kPrivacy:
            MessageBoxW(window,T(L"Numpad9 隐私政策 · 2026-10-13\n\nNumpad9 完全离线转换九键输入。不收集、保存或传输按键、输入文字、账号、设备标识或诊断数据；无广告、统计 SDK 或云服务。输入缓冲仅存在当前输入服务的内存中，用于组成候选并将你确认的文字交给当前文本应用。切换上下文或停用时清空未确认输入。\n\n练习框文字只在内存中，关闭窗口后丢弃。应用使用只读内置词库，不建立个人输入历史。安装、更新和卸载由安装程序及 Windows 处理。Microsoft Store 的数据处理适用微软政策；你输入文字的目标应用适用其自身政策。\n\n你可随时切换输入法，或通过 Windows 设置卸载 Numpad9。源代码及问题反馈：github.com/shoutoutuoadi325/Numpad9。",
                L"Numpad9 Privacy Policy · 2026-10-13\n\nNumpad9 converts nine-key input entirely offline. It does not collect, save, or transmit keystrokes, text, account information, device identifiers, or diagnostics. There are no ads, analytics SDKs, or cloud services. The current input buffer exists only in input-service memory to compose candidates and deliver confirmed text. Pending input is cleared when the context changes or the service deactivates.\n\nPractice text exists only in memory and is discarded on window close. The bundled dictionary is read-only; no personal typing history is created. Windows and the installer handle installation and removal. Microsoft Store and target text applications apply their own privacy policies.\n\nSwitch IMEs at any time, or uninstall via Windows Settings. Source and issues: github.com/shoutoutuoadi325/Numpad9."),T(L"隐私政策",L"Privacy policy"),MB_OK); return 0;
        case kAbout: {
            std::wstring about = T(L"Numpad9 0.1.0\n手机布局九键拼音与英文输入法。\n", L"Numpad9 0.1.0\nPhone-layout Pinyin and English input method.\n");
#ifdef NUMPAD9_MSIX
            about += L"Windows 11 24H2+ x64 · Microsoft Store MSIX\n";
#else
            about += L"Windows 10 1809+ / Windows 11 x64 · EXE\n";
#endif
            about += T(L"\n程序源码：GNU GPL v3。中文词库：CC-CEDICT，CC BY-SA 4.0。英文词表：SCOWL，适用随包许可。完整许可、署名和词库说明随应用提供。\n\n点击「确定」在浏览器中查看源码及完整许可；浏览器连接网络。",
                       L"\nSource: GNU GPL v3. Chinese dictionary: CC-CEDICT, CC BY-SA 4.0. English words: SCOWL, under its bundled licenses. Full licenses, attribution and dictionary notes accompany the app.\n\nOK opens source and full licenses in your browser using the network.");
            if (MessageBoxW(window, about.c_str(), T(L"关于与许可",L"About and licenses"), MB_OKCANCEL|MB_ICONINFORMATION)==IDOK)
                ShellExecuteW(window,L"open",L"https://github.com/shoutoutuoadi325/Numpad9",nullptr,nullptr,SW_SHOWNORMAL);
            return 0;
        }
        } break;
    case WM_DESTROY:
#ifdef NUMPAD9_MSIX
        shuttingDown = true; if (inputService) inputService->Stop();
        candidateWindow.Destroy(); inputService.reset(); RemoveTray(window);
#endif
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
void UninitializeRuntime() {
#ifdef NUMPAD9_MSIX
    RoUninitialize();
#else
    CoUninitialize();
#endif
}
int WINAPI wWinMain(HINSTANCE appInstance,HINSTANCE,PWSTR,int show) {
    instance=appInstance;
#ifdef NUMPAD9_MSIX
    struct MutexHandle { HANDLE value; ~MutexHandle() { if (value) CloseHandle(value); } };
    MutexHandle singleInstance{CreateMutexW(nullptr, FALSE, L"Local\\Numpad9.StoreHost")};
    if (!singleInstance.value) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"Numpad9.StoreHost", nullptr)) {
            ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing);
        }
        return 0;
    }
    numpad9::ime::gModule = instance;
    HRESULT comResult=RoInitialize(RO_INIT_SINGLETHREADED);
#else
    HRESULT comResult=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
#endif
    if(FAILED(comResult)) return 1;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    HMODULE rich=LoadLibraryW(L"Msftedit.dll");
    if(!rich) {UninitializeRuntime(); return 2;}
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=WindowProc; wc.hInstance=instance;
#ifdef NUMPAD9_MSIX
    wc.lpszClassName=L"Numpad9.StoreHost";
#else
    wc.lpszClassName=L"Numpad9.Onboarding";
#endif
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1)); wc.hIconSm=wc.hIcon;
    if(!RegisterClassExW(&wc)) {FreeLibrary(rich); UninitializeRuntime(); return 3;}
    UINT initialDpi=GetDpiForSystem(); dpi=initialDpi;
    RECT desktop{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&desktop,0);
    int width=Scale(1040),height=Scale(800);
    if(width>desktop.right-desktop.left) width=desktop.right-desktop.left;
    if(height>desktop.bottom-desktop.top) height=desktop.bottom-desktop.top;
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Numpad9",WS_OVERLAPPEDWINDOW|WS_VSCROLL,
        CW_USEDEFAULT,CW_USEDEFAULT,width,height,nullptr,nullptr,instance,nullptr);
    if(!window) {FreeLibrary(rich); UninitializeRuntime(); return 4;}
    ShowWindow(window,show); UpdateWindow(window);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(IsDialogMessageW(window,&message)) continue;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    for(HFONT font : {bodyFont,titleFont,smallFont,keyFont}) if(font) DeleteObject(font);
    FreeLibrary(rich); UninitializeRuntime(); return static_cast<int>(message.wParam);
}
