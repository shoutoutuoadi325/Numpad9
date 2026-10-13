#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <msctf.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "identity.h"

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
    SetWindowTextW(status, Registered()
        ? T(L"输入服务已安装。点击切换，或使用 Win + 空格选择 Numpad9。", L"Input service installed. Switch below or select Numpad9 with Win + Space.")
        : T(L"未检测到输入服务。请通过完整安装包安装；重新登录后再试。", L"Input service not detected. Use the full installer, then sign out and back in."));
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
    case WM_CREATE: mainWindow=window; dpi=GetDpiForWindow(window); CreateControls(); return 0;
    case WM_SIZE: Layout(); return 0;
    case WM_DPICHANGED: {
        dpi=HIWORD(wParam); auto rect=reinterpret_cast<RECT*>(lParam);
        Fonts(); SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE); Layout(); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto info=reinterpret_cast<MINMAXINFO*>(lParam); info->ptMinTrackSize={Scale(610),Scale(390)}; return 0;
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
            MessageBoxW(window,T(L"1. 安装后，点击「切换到 Numpad9」或按 Win + 空格选择 Numpad9。\n2. 看手机键位的位置，忽略电脑键帽数字。中文输入不带声调的完整拼音：nihao → 64426；按电脑小键盘 6 4 4 8 6。\n3. 空格 / Enter 确认候选；方向键 / 小键盘 * 选择；小键盘 + / − 翻页；Esc 取消；Backspace 删除。\n4. 小键盘 / 或 Ctrl + Shift + E 切换中英文；手机 1（电脑 7）输入逗号；小键盘小数点输入句号；小键盘 0 确认候选或输入空格。\n5. 可逐词确认组合成句。不支持云预测或个人词库学习。切回其他输入法即可正常使用小键盘。\n\n输入法在当前文本应用内工作；不适用于密码、安全桌面或不支持 TSF 的应用。", 
                L"1. Click Switch to Numpad9, or choose Numpad9 with Win + Space.\n2. Use phone key positions, ignoring PC keycap numbers. For Pinyin nihao (logical 64426), press PC numpad 6 4 4 8 6.\n3. Space / Enter commits; arrows / numpad * select; + / - pages; Esc cancels; Backspace deletes.\n4. Numpad / or Ctrl + Shift + E switches Chinese / English. Phone 1 (PC 7) inserts a comma; keypad decimal inserts a full stop; numpad 0 commits or inserts a space.\n5. Commit words to compose sentences. Cloud prediction and personal learning are not provided. Switch IMEs to restore numeric typing.\n\nUse ordinary TSF-compatible text fields. Passwords and secure desktops are excluded."),T(L"使用说明",L"Keyboard guide"),MB_OK); return 0;
        case kPrivacy:
            MessageBoxW(window,T(L"Numpad9 隐私政策 · 2026-10-13\n\nNumpad9 完全离线转换九键输入。不收集、保存或传输按键、输入文字、账号、设备标识或诊断数据；无广告、统计 SDK 或云服务。输入缓冲仅存在当前应用的内存中，用于组成候选并将你确认的文字交给当前文本应用。切换上下文或停用时清空未确认输入。\n\n练习框文字只在内存中，关闭窗口后丢弃。应用使用只读内置词库，不建立个人输入历史。安装、更新和卸载由安装程序及 Windows 处理。Microsoft Store 的数据处理适用微软政策；你输入文字的目标应用适用其自身政策。\n\n你可随时切换输入法，或通过 Windows 设置卸载 Numpad9。源代码及问题反馈：github.com/shoutoutuoadi325/Numpad9。",
                L"Numpad9 Privacy Policy · 2026-10-13\n\nNumpad9 converts nine-key input entirely offline. It does not collect, save, or transmit keystrokes, text, account information, device identifiers, or diagnostics. There are no ads, analytics SDKs, or cloud services. The current input buffer exists only in the text application's memory to compose candidates and deliver confirmed text. Pending input is cleared when the context changes or the service deactivates.\n\nPractice text exists only in memory and is discarded on window close. The bundled dictionary is read-only; no personal typing history is created. Windows and the installer handle installation and removal. Microsoft Store and target text applications apply their own privacy policies.\n\nSwitch IMEs at any time, or uninstall via Windows Settings. Source and issues: github.com/shoutoutuoadi325/Numpad9."),T(L"隐私政策",L"Privacy policy"),MB_OK); return 0;
        case kAbout:
            if(MessageBoxW(window,T(L"Numpad9 0.1.0\n手机布局九键拼音与英文输入法。Windows 10 1809+ / Windows 11 x64。\n\n程序源码：GNU GPL v3（仓库 LICENSE）。\n中文词库基于 CC-CEDICT，采用 CC BY-SA 4.0；转换与常用词排序详见随包提供的许可和词库说明。\n\n点击「确定」在浏览器中查看源码与完整许可；浏览器连接网络。", 
                L"Numpad9 0.1.0\nPhone-layout Pinyin and English IME. Windows 10 1809+ / Windows 11 x64.\n\nSource: GNU GPL v3 (repository LICENSE).\nChinese dictionary derived from CC-CEDICT, licensed CC BY-SA 4.0. Transformation and ranking notes accompany the installer.\n\nOK opens source and full licenses in your browser using the network."),T(L"关于与许可",L"About and licenses"),MB_OKCANCEL|MB_ICONINFORMATION)==IDOK)
                ShellExecuteW(window,L"open",L"https://github.com/shoutoutuoadi325/Numpad9",nullptr,nullptr,SW_SHOWNORMAL);
            return 0;
        } break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
int WINAPI wWinMain(HINSTANCE appInstance,HINSTANCE,PWSTR,int show) {
    instance=appInstance;
    HRESULT comResult=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(comResult)) return 1;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    HMODULE rich=LoadLibraryW(L"Msftedit.dll");
    if(!rich) {CoUninitialize(); return 2;}
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=WindowProc; wc.hInstance=instance;
    wc.lpszClassName=L"Numpad9.Onboarding"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1)); wc.hIconSm=wc.hIcon;
    if(!RegisterClassExW(&wc)) {FreeLibrary(rich); CoUninitialize(); return 3;}
    UINT initialDpi=GetDpiForSystem(); dpi=initialDpi;
    RECT desktop{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&desktop,0);
    int width=Scale(1040),height=Scale(800);
    if(width>desktop.right-desktop.left) width=desktop.right-desktop.left;
    if(height>desktop.bottom-desktop.top) height=desktop.bottom-desktop.top;
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Numpad9",WS_OVERLAPPEDWINDOW|WS_VSCROLL,
        CW_USEDEFAULT,CW_USEDEFAULT,width,height,nullptr,nullptr,instance,nullptr);
    if(!window) {FreeLibrary(rich); CoUninitialize(); return 4;}
    ShowWindow(window,show); UpdateWindow(window);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(IsDialogMessageW(window,&message)) continue;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    for(HFONT font : {bodyFont,titleFont,smallFont,keyFont}) if(font) DeleteObject(font);
    FreeLibrary(rich); CoUninitialize(); return static_cast<int>(message.wParam);
}
