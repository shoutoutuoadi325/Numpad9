// Windows-only COM and installed input smoke tests. No SendInput or global hooks.
// Successful registration/activation is tested separately with --registered.
// That path asks Windows to activate the installed profile, not a manually
// constructed service whose aggregate Activate HRESULT cannot locate a failure.
#include <windows.h>
#include <msctf.h>
#include <richedit.h>
#include <shellapi.h>
#include <iostream>
#include <cwchar>
#include <atomic>
#include <algorithm>
#include <new>
#include <string>
#include <vector>
#include "identity.h"
#include "tsf_compat.h"

// MinGW omits this SDK enumeration interface. Keep its exact COM vtable order.
#ifndef __IEnumTfUIElements_INTERFACE_DEFINED__
#define __IEnumTfUIElements_INTERFACE_DEFINED__
struct IEnumTfUIElements : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Clone(IEnumTfUIElements**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Next(ULONG, ITfUIElement**, ULONG*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset() = 0;
    virtual HRESULT STDMETHODCALLTYPE Skip(ULONG) = 0;
};
#endif

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

LRESULT CALLBACK TestWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INPUTLANGCHANGEREQUEST || message == WM_INPUTLANGCHANGE) {
        std::cerr << "Host language message=0x" << std::hex << message
            << ", requested=" << reinterpret_cast<HKL>(lparam)
            << ", current=" << GetKeyboardLayout(0) << '\n';
    }
    // Let Windows complete the language change and forward it to RichEdit.
    return DefWindowProcW(window, message, wparam, lparam);
}

void LogInputState(const char* stage, ITfInputProcessorProfiles* profiles,
                   ITfInputProcessorProfileMgr* manager, ITfKeystrokeMgr* keys) {
    LANGID language = 0;
    const HRESULT languageResult = profiles->GetCurrentLanguage(&language);
    TF_INPUTPROCESSORPROFILE selected{}; CLSID foreground{};
    const HRESULT selectedResult = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &selected);
    const HRESULT foregroundResult = keys->GetForeground(&foreground);
    std::cerr << stage << ": languageHr=0x" << std::hex << languageResult << ", language=0x" << language
        << ", hkl=" << GetKeyboardLayout(0) << ", activeHr=0x" << selectedResult
        << ", type=" << selected.dwProfileType << ", profileLanguage=0x" << selected.langid
        << ", foregroundHr=0x" << foregroundResult << '\n';
    LogGuid("stageActiveCLSID", selected.clsid); LogGuid("stageForegroundCLSID", foreground);
}

HRESULT SelectChineseLayout(HKL& selected, bool& newlyLoaded) {
    const int count = GetKeyboardLayoutList(0, nullptr);
    if (count <= 0 || count > 1024) return E_FAIL;
    std::vector<HKL> layouts(static_cast<std::size_t>(count));
    const int loaded = GetKeyboardLayoutList(count, layouts.data());
    if (loaded <= 0 || loaded > count) return E_FAIL;
    std::cerr << "Loaded keyboard layouts:";
    for (int i = 0; i < loaded; ++i) {
        std::cerr << ' ' << layouts[i];
        if (!selected && LOWORD(reinterpret_cast<ULONG_PTR>(layouts[i])) == numpad9::ime::kLanguage) selected = layouts[i];
    }
    std::cerr << '\n';
    if (!selected) {
        // 00000804 is Microsoft's documented built-in Chinese(Simplified)-US
        // layout. Win8+ LoadKeyboardLayout may affect the focused desktop, so
        // this disposable test restores its original HKL before closing.
        selected = LoadKeyboardLayoutW(L"00000804", KLF_ACTIVATE | KLF_SUBSTITUTE_OK);
        if (!selected) { const DWORD code = GetLastError(); return code ? HRESULT_FROM_WIN32(code) : E_FAIL; }
        newlyLoaded = std::find(layouts.begin(), layouts.begin() + loaded, selected) == layouts.begin() + loaded;
    }
    if (LOWORD(reinterpret_cast<ULONG_PTR>(selected)) != numpad9::ime::kLanguage) return E_FAIL;
    const HKL prior = ActivateKeyboardLayout(selected, KLF_SETFORPROCESS);
    std::cerr << "Native layout selection: previous=" << prior << ", requested=" << selected
        << ", immediate=" << GetKeyboardLayout(0) << '\n';
    if (!prior) {
        const DWORD code = GetLastError(); return code ? HRESULT_FROM_WIN32(code) : E_FAIL;
    }
    Pump(150);
    std::cerr << "Native layout after message pump=" << GetKeyboardLayout(0) << '\n';
    return LOWORD(reinterpret_cast<ULONG_PTR>(GetKeyboardLayout(0))) == numpad9::ime::kLanguage ? S_OK : E_FAIL;
}

// Read actual Windows-owned composition ranges under the host's TSF read lock.
// Checking only RichEdit's text would not distinguish a real IME composition
// from ordinary text insertion.
class CompositionRead final : public ITfEditSession {
    std::atomic<ULONG> references_{1};
    ITfContext* context_;
public:
    std::vector<std::wstring> own;
    UINT total = 0;
    explicit CompositionRead(ITfContext* context) : context_(context) { context_->AddRef(); }
    ~CompositionRead() { context_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, IID_ITfEditSession)) return E_NOINTERFACE;
        *out = static_cast<ITfEditSession*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG left = --references_; if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
        try {
            Com<ITfContextComposition> composition;
            Com<IEnumITfCompositionView> enumerator;
            HRESULT hr = context_->QueryInterface(IID_ITfContextComposition, composition.out());
            if (FAILED(hr)) return hr;
            hr = composition->EnumCompositions(&enumerator.p); if (FAILED(hr)) return hr;
            for (UINT i = 0; i < 16; ++i) {
                Com<ITfCompositionView> view;
                ULONG fetched = 0;
                hr = enumerator->Next(1, &view.p, &fetched);
                if (FAILED(hr)) return hr;
                if (!fetched || !view.p) return S_OK;
                ++total;
                CLSID owner{};
                hr = view->GetOwnerClsid(&owner); if (FAILED(hr)) return hr;
                if (!IsEqualGUID(owner, numpad9::ime::kClsid)) continue;
                Com<ITfRange> range;
                hr = view->GetRange(&range.p); if (FAILED(hr)) return hr;
                wchar_t buffer[256]{}; ULONG length = 0;
                hr = range->GetText(cookie, 0, buffer, 255, &length); if (FAILED(hr)) return hr;
                own.emplace_back(buffer, length);
            }
            return E_UNEXPECTED;
        } catch (...) { return E_OUTOFMEMORY; }
    }
};

bool InputChecks(ITfThreadMgr* thread, ITfKeystrokeMgr* keys, TfClientId client, HWND edit) {
    auto error = [](const char* operation, HRESULT hr = E_FAIL) {
        std::cerr << operation << ": 0x" << std::hex << hr << '\n'; return false;
    };
    auto textEquals = [&](const wchar_t* expected, const char* operation) {
        wchar_t buffer[512]{}; GetWindowTextW(edit, buffer, 512);
        if (std::wcscmp(buffer, expected) != 0) {
            std::cerr << operation << ": RichEdit text mismatch (UTF16 length="
                << std::dec << std::wcslen(buffer) << ")\n"; return false;
        }
        return true;
    };
    auto compositionEquals = [&](const wchar_t* expected, const char* operation) {
        Com<ITfDocumentMgr> document; Com<ITfContext> context;
        HRESULT hr = thread->GetFocus(&document.p);
        if (hr != S_OK || !document.p) return error(operation, hr);
        hr = document->GetTop(&context.p);
        if (hr != S_OK || !context.p) return error(operation, hr);
        Com<CompositionRead> session; session.p = new (std::nothrow) CompositionRead(context.p);
        if (!session.p) return error(operation, E_OUTOFMEMORY);
        HRESULT edited = E_FAIL;
        hr = context->RequestEditSession(client, session.p, TF_ES_SYNC | TF_ES_READ, &edited);
        if (hr != S_OK || edited != S_OK) return error(operation, FAILED(hr) ? hr : edited);
        const bool matches = expected ? session->total == 1 && session->own.size() == 1 && session->own[0] == expected
                                      : session->total == 0 && session->own.empty();
        if (!matches) {
            std::cerr << operation << ": actual TSF compositions=" << std::dec << session->total
                << ", owned=" << session->own.size() << '\n'; return false;
        }
        return true;
    };
    auto logCompartments = [&]() {
        Com<ITfDocumentMgr> document; Com<ITfContext> context;
        thread->GetFocus(&document.p); if (document.p) document->GetTop(&context.p);
        for (UINT owner = 0; owner < 2; ++owner) {
            Com<ITfCompartmentMgr> compartments;
            IUnknown* object = owner == 0 ? static_cast<IUnknown*>(thread) : static_cast<IUnknown*>(context.p);
            if (!object || FAILED(object->QueryInterface(IID_ITfCompartmentMgr, compartments.out()))) continue;
            const GUID* guids[] = {&GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &GUID_COMPARTMENT_KEYBOARD_DISABLED,
                &GUID_COMPARTMENT_EMPTYCONTEXT};
            const char* names[] = {"openClose", "disabled", "emptyContext"};
            for (UINT i = 0; i < 3; ++i) {
                Com<ITfCompartment> compartment; VARIANT value; VariantInit(&value);
                HRESULT hr = compartments->GetCompartment(*guids[i], &compartment.p);
                if (SUCCEEDED(hr) && compartment.p) hr = compartment->GetValue(&value);
                std::cerr << (owner == 0 ? "Thread " : "Context ") << names[i] << ": hr=0x"
                    << std::hex << hr << ", vt=" << value.vt;
                if (value.vt == VT_I4) std::cerr << ", value=" << value.lVal;
                std::cerr << '\n'; VariantClear(&value);
            }
        }
    };
    auto key = [&](UINT vk, UINT scan, bool extended = false, bool expected = true) {
        const LPARAM down = static_cast<LPARAM>(1U | ((scan & 0xffU) << 16) | (extended ? 0x01000000U : 0));
        BOOL eaten = FALSE;
        HRESULT hr = keys->TestKeyDown(vk, down, &eaten);
        if (hr != S_OK || (eaten != FALSE) != expected) {
            std::cerr << "TSF TestKeyDown vk=0x" << std::hex << vk << ", scan=0x" << scan
                << ", extended=" << extended << ", hr=0x" << hr << ", eaten=" << eaten << '\n';
            logCompartments(); return false;
        }
        if (!expected) return true; // A normal application would dispatch this key itself.
        eaten = FALSE; hr = keys->KeyDown(vk, down, &eaten);
        if (hr != S_OK || !eaten) {
            std::cerr << "TSF KeyDown vk=0x" << std::hex << vk << ", hr=0x" << hr << ", eaten=" << eaten << '\n';
            logCompartments(); return false;
        }
        const LPARAM up = down | static_cast<LPARAM>(0xc0000000U);
        eaten = FALSE; hr = keys->TestKeyUp(vk, up, &eaten);
        if (hr != S_OK || !eaten) return error("Consumed key's TestKeyUp", hr);
        eaten = FALSE; hr = keys->KeyUp(vk, up, &eaten);
        if (hr != S_OK || !eaten) return error("Consumed key's KeyUp", hr);
        Pump(15); return true;
    };
    auto candidate = [&]() -> ITfCandidateListUIElement* {
        Com<ITfUIElementMgr> ui; Com<IEnumTfUIElements> enumerator;
        if (thread->QueryInterface(numpad9::ime::kIidUiElementMgr, ui.out()) != S_OK ||
            ui->EnumUIElements(&enumerator.p) != S_OK || !enumerator.p) return nullptr;
        for (UINT i = 0; i < 128; ++i) {
            Com<ITfUIElement> element; ULONG fetched = 0;
            if (enumerator->Next(1, &element.p, &fetched) != S_OK || !fetched || !element.p) break;
            GUID guid{};
            if (element->GetGUID(&guid) != S_OK || !IsEqualGUID(guid, numpad9::ime::kCandidateUi)) continue;
            ITfCandidateListUIElement* found = nullptr;
            if (element->QueryInterface(numpad9::ime::kIidCandidateList, reinterpret_cast<void**>(&found)) == S_OK) return found;
        }
        return nullptr;
    };
    auto selectNihao = [&]() {
        Com<ITfCandidateListUIElement> list; list.p = candidate();
        if (!list.p) return error("Published TSF candidate list missing");
        UINT count = 0, selected = 0, wanted = 0; bool found = false;
        if (list->GetCount(&count) != S_OK || count == 0 || count > 100 || list->GetSelection(&selected) != S_OK)
            return error("Candidate list count/selection");
        for (UINT i = 0; i < count; ++i) {
            BSTR value = nullptr;
            const HRESULT hr = list->GetString(i, &value);
            if (hr != S_OK || !value) { if (value) SysFreeString(value); return error("Candidate text", hr); }
            if (std::wstring(value, SysStringLen(value)) == L"你好") { wanted = i; found = true; }
            SysFreeString(value);
        }
        if (!found) return error("Actual installed dictionary candidate 你好 missing");
        // Exercise real candidate movement even if 你好 is already first.
        const UINT original = selected;
        if (!key(VK_MULTIPLY, 0x37)) return false;
        if (list->GetSelection(&selected) != S_OK || selected != (original + 1) % count)
            return error("Next candidate selection");
        if (!key(VK_LEFT, 0x4b, true)) return false;
        if (list->GetSelection(&selected) != S_OK || selected != original) return error("Previous candidate selection");
        for (UINT i = 0; selected != wanted && i < count; ++i) {
            if (!key(VK_MULTIPLY, 0x37) || list->GetSelection(&selected) != S_OK) return false;
        }
        if (selected != wanted) return error("Select actual 你好 candidate");
        return true;
    };

    SetWindowTextW(edit, L""); SetFocus(edit); Pump(40);
    if (!compositionEquals(nullptr, "Initially no composition") ||
        !key('6', 0x07, false, false) || !key(VK_RIGHT, 0x4d, true, false) ||
        !textEquals(L"", "Ordinary keys passed through")) return false;
    const UINT physical[] = {VK_NUMPAD6, VK_NUMPAD4, VK_NUMPAD4, VK_NUMPAD8, VK_NUMPAD6};
    const UINT scans[] = {0x4d, 0x4b, 0x4b, 0x48, 0x4d};
    const wchar_t* prefixes[] = {L"6", L"64", L"644", L"6442", L"64426"};
    for (UINT i = 0; i < 5; ++i) if (!key(physical[i], scans[i]) ||
        !textEquals(prefixes[i], "Phone layout visible preedit") ||
        !compositionEquals(prefixes[i], "Actual TSF phone-layout composition")) return false;
    if (!key(VK_BACK, 0x0e) || !textEquals(L"6442", "Backspace preedit") ||
        !compositionEquals(L"6442", "Backspace composition") || !key(VK_NUMPAD6, 0x4d) ||
        !selectNihao() || !key(VK_SPACE, 0x39) || !textEquals(L"你好", "Space commits actual selected candidate") ||
        !compositionEquals(nullptr, "Commit ends actual composition")) return false;
    {
        Com<ITfCandidateListUIElement> ended; ended.p = candidate();
        if (ended.p) return error("Candidate UI element retained after commit");
    }
    const wchar_t* baseline = L"left replace right";
    SetWindowTextW(edit, baseline); CHARRANGE selection{5, 12};
    SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection)); Pump(30);
    if (!key(VK_NUMPAD6, 0x4d) || !textEquals(L"left 6 right", "Compose over selected text") ||
        !compositionEquals(L"6", "Selection replacement is a composition") ||
        !key(VK_ESCAPE, 0x01) || !textEquals(baseline, "Escape restores original selected text") ||
        !compositionEquals(nullptr, "Escape ends composition")) return false;
    SetWindowTextW(edit, L""); Pump(30);
    // Windows uses nonextended navigation VKs for the same physical keypad
    // positions with Num Lock off. Dedicated extended arrows remain distinct.
    const UINT unlocked[] = {VK_RIGHT, VK_LEFT, VK_LEFT, VK_UP, VK_RIGHT};
    for (UINT i = 0; i < 5; ++i) if (!key(unlocked[i], scans[i]) ||
        !compositionEquals(prefixes[i], "Num Lock-off actual composition")) return false;
    if (!selectNihao() || !key(VK_RETURN, 0x1c) || !textEquals(L"你好", "Num Lock-off Enter commit") ||
        !compositionEquals(nullptr, "Num Lock-off composition ended")) return false;
    // Turning the IME off while this same editor keeps focus must release its
    // existing composition/UI and preserve raw preedit, not trap subsequent
    // numeric input or silently replace it with a selected candidate.
    for (UINT scenario = 0; scenario < 2; ++scenario) {
        SetWindowTextW(edit, L""); Pump(30);
        if (!key(VK_NUMPAD6, 0x4d) || !compositionEquals(L"6", "Composition before compartment disable")) return false;
        Com<ITfCompartmentMgr> compartments;
        Com<ITfDocumentMgr> document; Com<ITfContext> context;
        HRESULT hr;
        if (scenario == 0) hr = thread->QueryInterface(IID_ITfCompartmentMgr, compartments.out());
        else {
            hr = thread->GetFocus(&document.p);
            if (hr == S_OK && document.p) hr = document->GetTop(&context.p);
            if (hr == S_OK && context.p) hr = context->QueryInterface(IID_ITfCompartmentMgr, compartments.out());
        }
        if (hr != S_OK || !compartments.p) return error("Disable compartment manager", hr);
        const GUID& guid = scenario == 0 ? GUID_COMPARTMENT_KEYBOARD_OPENCLOSE : GUID_COMPARTMENT_KEYBOARD_DISABLED;
        Com<ITfCompartment> compartment;
        hr = compartments->GetCompartment(guid, &compartment.p);
        if (hr != S_OK || !compartment.p) return error("Get disable compartment", hr);
        VARIANT original; VariantInit(&original);
        hr = compartment->GetValue(&original);
        if (FAILED(hr)) { VariantClear(&original); return error("Read original disable compartment", hr); }
        VARIANT value; VariantInit(&value); value.vt = VT_I4; value.lVal = scenario == 0 ? 0 : 1;
        hr = compartment->SetValue(client, &value);
        bool passed = hr == S_OK;
        if (passed) {
            Pump(200);
            passed = compositionEquals(nullptr, "Disabling ends actual owned composition") &&
                textEquals(L"6", "Disabling preserves raw preedit") && key(VK_NUMPAD6, 0x4d, false, false);
            Com<ITfCandidateListUIElement> closed; closed.p = candidate();
            if (closed.p) { passed = false; error("Disabling leaves a published candidate UI"); }
        } else error("Set disable compartment", hr);
        const HRESULT restored = original.vt == VT_EMPTY ? compartments->ClearCompartment(client, guid)
                                                        : compartment->SetValue(client, &original);
        VariantClear(&original); Pump(100);
        if (restored != S_OK) return error("Restore original input compartment", restored);
        if (!passed) return false;
        SetWindowTextW(edit, L""); Pump(30);
        if (!key(VK_NUMPAD6, 0x4d) || !compositionEquals(L"6", "Re-enabled composition") ||
            !key(VK_ESCAPE, 0x01) || !compositionEquals(nullptr, "Re-enabled cancel")) return false;
    }
    std::cout << "Real TSF key dispatch, phone/Num Lock-off compositions, candidate selection, commits, selection cancellation and disabled-context cleanup passed.\n";
    return true;
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
    const HKL previousLayout = GetKeyboardLayout(0);
    HKL chineseLayout = nullptr;
    bool newlyLoadedLayout = false;
    TF_INPUTPROCESSORPROFILE installed{};
    const HRESULT installedResult = manager->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR,
        numpad9::ime::kLanguage, numpad9::ime::kClsid, numpad9::ime::kProfile, nullptr, &installed);
    std::cerr << "Installed own profile: hr=0x" << std::hex << installedResult
        << ", flags=0x" << installed.dwFlags << ", caps=0x" << installed.dwCaps << '\n';
    LogGuid("installedCategory", installed.catid);

    TfClientId client = 0;
    if (!check(thread->Activate(&client), "Activate TSF host thread")) return result;
    HMODULE richEdit = LoadLibraryW(L"msftedit.dll");
    WNDCLASSW windowClass{};
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpfnWndProc = TestWindowProc;
    windowClass.lpszClassName = L"Numpad9.RegisteredSmoke";
    const ATOM registeredClass = richEdit ? RegisterClassW(&windowClass) : 0;
    HWND window = registeredClass ? CreateWindowExW(0, windowClass.lpszClassName, L"Numpad9 registered TSF test",
        WS_OVERLAPPEDWINDOW, 20, 20, 400, 160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) : nullptr;
    HWND edit = window ? CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE |
        WS_TABSTOP | ES_MULTILINE, 10, 10, 360, 90, window, nullptr, GetModuleHandleW(nullptr), nullptr) : nullptr;
    if (!edit) {
        std::cerr << "Create TSF RichEdit host: " << GetLastError() << '\n'; result = 11;
    } else {
        SendMessageW(edit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
        const LRESULT languageOptions = SendMessageW(edit, EM_GETLANGOPTIONS, 0, 0);
        SendMessageW(edit, EM_SETLANGOPTIONS, 0, languageOptions & ~IMF_AUTOKEYBOARD);
        std::cerr << "RichEdit original language options=0x" << std::hex << languageOptions
            << ", current=0x" << SendMessageW(edit, EM_GETLANGOPTIONS, 0, 0) << '\n';
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
            LogInputState("After TSF language change", profiles.p, manager.p, keys.p);
            if (check(SelectChineseLayout(chineseLayout, newlyLoadedLayout), "Activate actual Chinese thread keyboard layout") &&
                check(manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, numpad9::ime::kLanguage,
                    numpad9::ime::kClsid, numpad9::ime::kProfile, nullptr, TF_IPPMF_FORPROCESS | TF_IPPMF_ENABLEPROFILE),
                    "Activate installed profile after native language selection")) {
            LogInputState("After HKL and exact TSF profile activation", profiles.p, manager.p, keys.p);
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
            } else if (!InputChecks(thread.p, keys.p, client, edit)) {
                result = 13;
            }
            }
        }
    }
    // Always restore process state and the user's existing enabled flag, also
    // on activation failure. Failure to restore is a test failure itself.
    if (previousLayout && chineseLayout && !ActivateKeyboardLayout(previousLayout, KLF_SETFORPROCESS)) {
        const DWORD code = GetLastError(); check(code ? HRESULT_FROM_WIN32(code) : E_FAIL, "Restore original thread keyboard layout");
    }
    Pump(100);
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
    if (newlyLoadedLayout && chineseLayout && !UnloadKeyboardLayout(chineseLayout)) {
        const DWORD code = GetLastError(); check(code ? HRESULT_FROM_WIN32(code) : E_FAIL, "Unload test-only Chinese layout");
    }
    if (window) DestroyWindow(window);
    Pump(100);
    check(thread->Deactivate(), "Deactivate TSF host thread");
    if (registeredClass) UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
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
