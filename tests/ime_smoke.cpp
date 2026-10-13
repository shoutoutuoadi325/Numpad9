// Windows-only COM lifecycle smoke test. No registry writes or SendInput.
// Successful registration/activation is tested separately with --registered.
// That path asks Windows to activate the installed profile, not a manually
// constructed service whose aggregate Activate HRESULT cannot locate a failure.
#include <windows.h>
#include <msctf.h>
#include <richedit.h>
#include <shellapi.h>
#include <iostream>
#include <cwchar>
#include "identity.h"

#ifndef TF_IPPMF_FORPROCESS
#define TF_IPPMF_FORPROCESS 0x10000000
#define TF_IPPMF_ENABLEPROFILE 0x00000001
#endif
#ifndef TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE
#define TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE 0x00000004
#endif

namespace {
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    void** out() { return reinterpret_cast<void**>(&p); }
    T* operator->() const { return p; }
};

void Pump(DWORD milliseconds) {
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (GetTickCount64() >= deadline) break;
        MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (true);
}
void LogGuid(const char* label, const GUID& value) {
    wchar_t text[40]{}; StringFromGUID2(value, text, 40);
    std::cerr << label << '=';
    for (wchar_t character : text) { if (!character) break; std::cerr << static_cast<char>(character); }
    std::cerr << '\n';
}

int RegisteredSmoke() {
    Com<ITfThreadMgr> thread;
    Com<ITfInputProcessorProfiles> profiles;
    Com<ITfInputProcessorProfileMgr> manager;
    Com<ITfKeystrokeMgr> keys;
    int result = 0;
    auto check = [&result](HRESULT hr, const char* operation) {
        if (hr != S_OK) {
            std::cerr << operation << ": 0x" << std::hex << hr << '\n'; result = 10;
        }
        return hr == S_OK;
    };
    if (!check(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfThreadMgr, thread.out()), "Create TSF thread manager") ||
        !check(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles, profiles.out()), "Create installed profiles manager") ||
        !check(profiles->QueryInterface(IID_ITfInputProcessorProfileMgr, manager.out()), "Profile manager interface") ||
        !check(thread->QueryInterface(IID_ITfKeystrokeMgr, keys.out()), "Keystroke manager interface")) return result;

    TF_INPUTPROCESSORPROFILE previous{};
    const bool hadPrevious = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &previous) == S_OK;
    LANGID previousLanguage = 0;
    const bool hadLanguage = profiles->GetCurrentLanguage(&previousLanguage) == S_OK;
    BOOL previouslyEnabled = FALSE;
    if (!check(profiles->IsEnabledLanguageProfile(numpad9::ime::kClsid,
        numpad9::ime::kLanguage, numpad9::ime::kProfile, &previouslyEnabled),
        "Installed profile enablement")) return result;

    TfClientId client = 0;
    if (!check(thread->Activate(&client), "Activate TSF host thread")) return result;
    HMODULE richEdit = LoadLibraryW(L"msftedit.dll");
    HWND window = richEdit ? CreateWindowExW(0, L"STATIC", L"Numpad9 registered TSF test",
        WS_OVERLAPPEDWINDOW, 20, 20, 400, 160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) : nullptr;
    HWND edit = window ? CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE |
        WS_TABSTOP | ES_MULTILINE, 10, 10, 360, 90, window, nullptr, GetModuleHandleW(nullptr), nullptr) : nullptr;
    if (!edit) {
        std::cerr << "Create TSF RichEdit host: " << GetLastError() << '\n'; result = 11;
    } else {
        SendMessageW(edit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
        ShowWindow(window, SW_SHOW); SetForegroundWindow(window); SetActiveWindow(window); SetFocus(edit);
        Pump(150);
        // Queue the selected profile before switching languages, as documented
        // by DONTCARECURRENTINPUTLANGUAGE. Scope remains this test process.
        if (check(profiles->EnableLanguageProfile(numpad9::ime::kClsid, numpad9::ime::kLanguage,
            numpad9::ime::kProfile, TRUE), "Enable installed profile") &&
            check(manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, numpad9::ime::kLanguage,
                numpad9::ime::kClsid, numpad9::ime::kProfile, nullptr,
                TF_IPPMF_FORPROCESS | TF_IPPMF_ENABLEPROFILE | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE),
                "Queue installed Numpad9 profile") &&
            check(profiles->ChangeCurrentLanguage(numpad9::ime::kLanguage), "Select test input language")) {
            bool active = false;
            const ULONGLONG deadline = GetTickCount64() + 3000;
            do {
                Pump(30);
                TF_INPUTPROCESSORPROFILE selected{};
                CLSID foreground = CLSID_NULL;
                Com<ITfDocumentMgr> document;
                Com<ITfContext> context;
                active = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &selected) == S_OK &&
                    selected.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR &&
                    selected.langid == numpad9::ime::kLanguage &&
                    IsEqualGUID(selected.clsid, numpad9::ime::kClsid) &&
                    IsEqualGUID(selected.guidProfile, numpad9::ime::kProfile) &&
                    keys->GetForeground(&foreground) == S_OK && IsEqualGUID(foreground, numpad9::ime::kClsid) &&
                    thread->GetFocus(&document.p) == S_OK && document.p &&
                    document->GetTop(&context.p) == S_OK && context.p;
                if (active) break;
            } while (GetTickCount64() < deadline);
            if (!active) {
                TF_INPUTPROCESSORPROFILE selected{}; CLSID foreground = CLSID_NULL;
                Com<ITfDocumentMgr> document; Com<ITfContext> context; Com<ITfThreadMgrEx> extended;
                const HRESULT profileResult = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &selected);
                const HRESULT foregroundResult = keys->GetForeground(&foreground);
                const HRESULT focusResult = thread->GetFocus(&document.p);
                const HRESULT contextResult = document.p ? document->GetTop(&context.p) : E_UNEXPECTED;
                DWORD flags = 0;
                const HRESULT flagsResult = thread->QueryInterface(IID_ITfThreadMgrEx, extended.out());
                if (SUCCEEDED(flagsResult)) extended->GetActiveFlags(&flags);
                LANGID language = 0; profiles->GetCurrentLanguage(&language);
                BOOL enabled = FALSE; profiles->IsEnabledLanguageProfile(numpad9::ime::kClsid,
                    numpad9::ime::kLanguage, numpad9::ime::kProfile, &enabled);
                std::cerr << "Installed profile did not become a foreground key service with an edit context; "
                    << "active=0x" << std::hex << profileResult << ", foreground=0x" << foregroundResult
                    << ", focus=0x" << focusResult << ", context=0x" << contextResult
                    << ", activeFlags=0x" << flags << ", client=0x" << client
                    << ", profileType=" << selected.dwProfileType << ", profileLanguage=" << selected.langid
                    << ", currentLanguage=" << language << ", enabled=" << enabled
                    << ", keyboardLayout=" << GetKeyboardLayout(0)
                    << ", editStyle=0x" << SendMessageW(edit, EM_GETEDITSTYLE, 0, 0)
                    << ", focusedEdit=" << (GetFocus() == edit)
                    << ", foregroundWindow=" << (GetForegroundWindow() == window) << '\n';
                LogGuid("selectedCLSID", selected.clsid); LogGuid("selectedProfile", selected.guidProfile);
                LogGuid("foregroundCLSID", foreground);
                result = 12;
            }
        }
    }
    // Always restore process state and the user's existing enabled flag, also
    // on activation failure. Failure to restore is a test failure itself.
    if (hadLanguage) check(profiles->ChangeCurrentLanguage(previousLanguage), "Restore input language");
    if (hadPrevious) check(manager->ActivateProfile(previous.dwProfileType, previous.langid,
        previous.clsid, previous.guidProfile, previous.hkl, TF_IPPMF_FORPROCESS), "Restore process input profile");
    else {
        const HRESULT hr = manager->DeactivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, numpad9::ime::kLanguage,
            numpad9::ime::kClsid, numpad9::ime::kProfile, nullptr, TF_IPPMF_FORPROCESS);
        if (FAILED(hr)) check(hr, "Deactivate test profile");
    }
    check(profiles->EnableLanguageProfile(numpad9::ime::kClsid, numpad9::ime::kLanguage,
        numpad9::ime::kProfile, previouslyEnabled), "Restore user profile enablement");
    if (window) DestroyWindow(window);
    Pump(100);
    check(thread->Deactivate(), "Deactivate TSF host thread");
    if (richEdit) FreeLibrary(richEdit);
    if (!result) std::cout << "Installed TSF profile, foreground key sink and RichEdit context checks passed.\n";
    return result;
}
}

int RunSmoke(int argc, wchar_t** argv) {
    const bool registered = argc == 3 && std::wcscmp(argv[2], L"--registered") == 0;
    if (argc != 2 && !registered) { std::cerr << "Expected absolute IME DLL path [--registered]\n"; return 1; }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) { CoUninitialize(); std::cerr << "DLL load failed\n"; return 3; }
    using GetClass = HRESULT (STDAPICALLTYPE *)(REFCLSID, REFIID, void**);
    using CanUnload = HRESULT (STDAPICALLTYPE *)();
    auto getClass = reinterpret_cast<GetClass>(GetProcAddress(dll, "DllGetClassObject"));
    auto canUnload = reinterpret_cast<CanUnload>(GetProcAddress(dll, "DllCanUnloadNow"));
    int result = 0;
    IClassFactory* factory = nullptr;
    ITfTextInputProcessor* tip = nullptr;
    auto check = [&result](HRESULT hr, const char* operation) {
        if (FAILED(hr)) { std::cerr << operation << ": 0x" << std::hex << hr << '\n'; result = 4; }
        return SUCCEEDED(hr);
    };
    if (!getClass || !canUnload || !GetProcAddress(dll, "DllRegisterServer") ||
        !GetProcAddress(dll, "DllUnregisterServer")) { result = 5; }
    if (!result && check(getClass(numpad9::ime::kClsid, IID_IClassFactory,
        reinterpret_cast<void**>(&factory)), "Get class factory") &&
        check(factory->CreateInstance(nullptr, IID_ITfTextInputProcessor,
        reinterpret_cast<void**>(&tip)), "Create TIP")) {
        if (canUnload() != S_FALSE) { std::cerr << "Live COM objects not counted\n"; result = 6; }
        if (tip->Activate(nullptr, 0) != E_INVALIDARG) { std::cerr << "Invalid activation accepted\n"; result = 7; }
        check(tip->Deactivate(), "Deactivate inactive TIP");
        ITfKeyEventSink* keys = nullptr;
        if (check(tip->QueryInterface(IID_ITfKeyEventSink, reinterpret_cast<void**>(&keys)), "Key sink interface")) {
            BOOL eaten = TRUE;
            check(keys->OnTestKeyDown(nullptr, VK_NUMPAD6, 0, &eaten), "Ignore missing input context");
            if (eaten) { std::cerr << "Key consumed without input context\n"; result = 8; }
            keys->Release();
        }
    }
    if (tip) tip->Release();
    if (factory) factory->Release();
    if (!result && canUnload() != S_OK) { std::cerr << "COM references leaked\n"; result = 6; }
    // Do not unload a module whose COM objects remain live.
    if (!canUnload || canUnload() == S_OK) FreeLibrary(dll);
    if (!result && registered) result = RegisteredSmoke();
    CoUninitialize();
    if (!result) std::cout << "TSF COM factory, invalid-state handling and unload checks passed.\n";
    return result;
}

// A real GUI host matches the desktop applications this input service targets.
// Keeping a console subsystem here can select TSF's console activation mode.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    const int result = RunSmoke(argc, argv);
    LocalFree(argv); return result;
}
