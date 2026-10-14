#include "tsf_compat.h"
#include "com.h"
#include "identity.h"
#include "candidate_window.h"
#include "display_attribute.h"
#include "language_bar.h"
#include <inputscope.h>
#include <oleauto.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

namespace numpad9::ime {
HINSTANCE gModule = nullptr;
std::atomic<long> gObjects{0};
std::atomic<long> gServerLocks{0};

namespace {
constexpr DWORD kInvalidCookie = 0xffffffffu;
constexpr DWORD kCandidateChanges = 0x3fu; // TF_CLUIE_*: all six candidate fields.

std::wstring ModulePath() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(gModule, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) return {};
        if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
        if (buffer.size() >= 32768) return {};
        buffer.resize(buffer.size() * 2);
    }
}
std::string Utf8(const std::wstring& text) {
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
bool ReadCompartment(IUnknown* owner, REFGUID guid, LONG& value) {
    ComPtr<ITfCompartmentMgr> manager; ComPtr<ITfCompartment> compartment;
    if (!owner || FAILED(owner->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(manager.put()))) ||
        FAILED(manager->GetCompartment(guid, compartment.put()))) return false;
    VARIANT variant; VariantInit(&variant);
    const HRESULT hr = compartment->GetValue(&variant);
    const bool present = SUCCEEDED(hr) && variant.vt == VT_I4;
    if (present) value = variant.lVal;
    VariantClear(&variant); return present;
}
HWND CurrentViewWindow(ITfContext* context) {
    ComPtr<ITfContextView> view;
    HWND window = nullptr;
    if (context->GetActiveView(view.put()) != S_OK || !view ||
        view->GetWnd(&window) != S_OK || !window || !IsWindow(window)) return nullptr;
    return window;
}
bool IsClassicInputControl(HWND window) {
    wchar_t name[128]{};
    if (!window || !GetClassNameW(window, name, static_cast<int>(std::size(name)))) return false;
    for (const wchar_t* known : {L"EDIT", L"RICHEDIT50W", L"RichEdit20W", L"RichEdit20A"})
        if (CompareStringOrdinal(name, -1, known, -1, TRUE) == CSTR_EQUAL) return true;
    return false;
}
bool NativeInputStyleAllowed(HWND window) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if (!style && GetLastError() != ERROR_SUCCESS) return false;
    return (style & (ES_PASSWORD | ES_READONLY)) == 0;
}
bool NativeDefaultScopeAllowed(ITfContext* context, ITfReadOnlyProperty* property,
                              TfEditCookie cookie, bool allowVirtualRange) {
    // A compatibility rule for observed stock-control DEFAULT metadata only:
    // never apply it to custom/web renderers, an unfocused window, or a native
    // password/read-only control. No control/document text is inspected.
    const HWND window = CurrentViewWindow(context);
    if (!window || GetFocus() != window || !IsClassicInputControl(window) || !NativeInputStyleAllowed(window)) return false;
    ComPtr<IEnumTfRanges> ranges;
    if (property->EnumRanges(cookie, ranges.put(), nullptr) != S_OK || !ranges) return false;
    ComPtr<ITfRange> declared;
    ULONG fetched = 0;
    const HRESULT first = ranges->Next(1, declared.put(), &fetched);
    if (first == S_FALSE && !fetched && !declared) return true;
    if (!allowVirtualRange || first != S_OK || fetched != 1 || !declared) return false;
    // RichEdit exposes exactly one virtual attribute range when DEFAULT is
    // undeclared. Both the selection and this range return E_FAIL/VT_EMPTY.
    // Any declared value, extra range or different error rejects conversion.
    VARIANT value; VariantInit(&value);
    const HRESULT valueResult = property->GetValue(cookie, declared.get(), &value);
    const bool nativeDefault = valueResult == E_FAIL && value.vt == VT_EMPTY;
    VariantClear(&value);
    if (!nativeDefault) return false;
    fetched = 0;
    return ranges->Next(1, declared.put(), &fetched) == S_FALSE && !fetched && !declared;
}
// Query only the current selection's input scope, never document contents.
bool ScopeAllowed(ITfContext* context, TfEditCookie cookie) {
    // Native password protection applies even if a host declares a permissive
    // input scope. Other controls use their application-owned scope normally.
    const HWND window = CurrentViewWindow(context);
    if (IsClassicInputControl(window) && !NativeInputStyleAllowed(window)) return false;
    TF_SELECTION selection{}; ULONG fetched = 0;
    if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || fetched != 1 || !selection.range) return false;
    ComPtr<ITfRange> range; range.reset(selection.range);
    // Input scope belongs to the host application, rather than a writable
    // text-service property. ITfInputScope's documented retrieval path uses
    // GetAppProperty and ITfReadOnlyProperty under the current TSF read lock.
    ComPtr<ITfReadOnlyProperty> property;
    if (context->GetAppProperty(kInputScopeProperty, property.put()) != S_OK || !property) return false;
    VARIANT variant; VariantInit(&variant);
    const HRESULT hr = property->GetValue(cookie, range.get(), &variant);
    bool allowed = hr == S_OK && variant.vt == VT_EMPTY;
    // S_FALSE may mean mixed coverage: only a genuinely empty enumeration can
    // admit it. The bounded virtual-range exception is exclusive to the observed
    // stock native DEFAULT E_FAIL/VT_EMPTY case.
    if ((hr == E_FAIL || hr == S_FALSE) && variant.vt == VT_EMPTY)
        allowed = NativeDefaultScopeAllowed(context, property.get(), cookie, hr == E_FAIL);
    if (SUCCEEDED(hr) && variant.vt == VT_UNKNOWN && variant.punkVal) {
        ComPtr<ITfInputScope> scope;
        allowed = SUCCEEDED(variant.punkVal->QueryInterface(kIidInputScope, reinterpret_cast<void**>(scope.put())));
        if (allowed) {
            InputScope* scopes = nullptr; UINT count = 0;
            allowed = SUCCEEDED(scope->GetInputScopes(&scopes, &count)) && count <= 1024 && (!count || scopes);
            if (allowed) for (UINT i = 0; i < count; ++i) {
                if (scopes[i] == IS_PASSWORD || scopes[i] == IS_NUMERIC_PASSWORD || scopes[i] == IS_PRIVATE ||
                    scopes[i] == IS_NUMERIC_PIN || scopes[i] == IS_ALPHANUMERIC_PIN || scopes[i] == IS_ALPHANUMERIC_PIN_SET) allowed = false;
            }
            CoTaskMemFree(scopes);
        }
    }
    VariantClear(&variant); return allowed;
}
HRESULT ReadOriginal(ITfRange* range, TfEditCookie cookie, std::wstring& text) {
    ComPtr<ITfRange> cursor;
    HRESULT hr = range->Clone(cursor.put()); if (FAILED(hr)) return hr;
    text.clear();
    for (;;) {
        wchar_t buffer[512]; ULONG fetched = 0;
        hr = cursor->GetText(cookie, TF_TF_MOVESTART, buffer, 512, &fetched);
        if (FAILED(hr)) return hr;
        if (!fetched) return S_OK;
        if (text.size() + fetched > 65536) return E_OUTOFMEMORY;
        text.append(buffer, fetched);
    }
}

class ScopeSession final : public RefCount, public ITfEditSession {
    ComPtr<ITfContext> context_;
public:
    bool allowed = false;
    explicit ScopeSession(ITfContext* context) : context_(context) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfEditSession) return E_NOINTERFACE;
        *result = static_cast<ITfEditSession*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return Retain(); }
    STDMETHODIMP_(ULONG) Release() override { return Drop(); }
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override { allowed = ScopeAllowed(context_.get(), cookie); return S_OK; }
};

// A deactivating service may not receive a synchronous write lock. End only the
// specific old composition asynchronously, without accessing later service state
// or overwriting any application text after focus has changed.
class TerminationSession final : public RefCount, public ITfEditSession {
    ComPtr<ITfContext> context_;
    ComPtr<ITfComposition> composition_;
public:
    TerminationSession(ITfContext* context, ITfComposition* composition) : context_(context), composition_(composition) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfEditSession) return E_NOINTERFACE;
        *result = static_cast<ITfEditSession*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return Retain(); }
    STDMETHODIMP_(ULONG) Release() override { return Drop(); }
    STDMETHODIMP DoEditSession(TfEditCookie cookie) override { return composition_->EndComposition(cookie); }
};

class Service;
enum class Action { None, Digit, Backspace, Cancel, Commit, Space, Comma, Period, Toggle, Refresh, DiscardDisabled,
                    Next, Previous, PageNext, PagePrevious, CommitAndPass, SetSelection };
struct Command { Action action = Action::None; char digit = 0; UINT index = 0; bool keepSelection = false; };

class CandidateElement final : public RefCount, public ITfCandidateListUIElementBehavior {
    Service* owner_;
    ComPtr<ITfDocumentMgr> document_;
    std::vector<Candidate> candidates_;
    std::vector<UINT> pages_;
    UINT selected_ = 0;
    BOOL shown_ = TRUE;
    DWORD changes_ = kCandidateChanges;
public:
    explicit CandidateElement(Service* owner, ITfDocumentMgr* document) : owner_(owner), document_(document) {}
    void Detach() { owner_ = nullptr; }
    void Update(const std::vector<Candidate>& candidates, size_t selected) {
        candidates_ = candidates; selected_ = static_cast<UINT>(selected); pages_.clear();
        for (UINT i = 0; i < candidates_.size(); i += 9) pages_.push_back(i);
        if (pages_.empty()) pages_.push_back(0);
        changes_ = kCandidateChanges;
    }
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == kIidUiElement || iid == kIidCandidateList || iid == kIidCandidateBehavior)
            *result = static_cast<ITfCandidateListUIElementBehavior*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return Retain(); }
    STDMETHODIMP_(ULONG) Release() override { return Drop(); }
    STDMETHODIMP GetDescription(BSTR* description) override {
        if (!description) return E_POINTER;
        *description = SysAllocString(L"Numpad9 candidate list"); return *description ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetGUID(GUID* guid) override { if (!guid) return E_POINTER;
        *guid = kCandidateUi; return S_OK; }
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP IsShown(BOOL* shown) override { if (!shown) return E_POINTER;
        *shown = shown_; return S_OK; }
    STDMETHODIMP GetUpdatedFlags(DWORD* flags) override { if (!flags) return E_POINTER;
        *flags = changes_; changes_ = 0; return S_OK; }
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** result) override {
        if (!result) return E_POINTER;
        *result = document_.get(); if (*result) (*result)->AddRef(); return *result ? S_OK : E_FAIL;
    }
    STDMETHODIMP GetCount(UINT* count) override { if (!count) return E_POINTER;
        *count = static_cast<UINT>(candidates_.size()); return S_OK; }
    STDMETHODIMP GetSelection(UINT* index) override { if (!index) return E_POINTER;
        *index = selected_; return S_OK; }
    STDMETHODIMP GetString(UINT index, BSTR* text) override {
        if (!text) return E_POINTER;
        *text = nullptr;
        if (index >= candidates_.size()) return E_INVALIDARG;
        *text = SysAllocString(candidates_[index].text.c_str()); return *text ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetPageIndex(UINT* index, UINT size, UINT* count) override {
        if (!count || (size && !index)) return E_POINTER;
        *count = static_cast<UINT>(pages_.size());
        const auto copied = std::min<size_t>(size, pages_.size());
        for (size_t i = 0; i < copied; ++i) index[i] = pages_[i];
        return size >= pages_.size() ? S_OK : S_FALSE;
    }
    STDMETHODIMP SetPageIndex(UINT* index, UINT count) override {
        if (!index || !count || count > candidates_.size() + 1 || index[0] != 0) return E_INVALIDARG;
        for (UINT i = 0; i < count; ++i) if ((i && index[i] <= index[i - 1]) || index[i] >= std::max<size_t>(1, candidates_.size())) return E_INVALIDARG;
        pages_.assign(index, index + count); changes_ |= 0x30; return S_OK;
    }
    STDMETHODIMP GetCurrentPage(UINT* page) override {
        if (!page) return E_POINTER;
        *page = 0;
        for (UINT i = 0; i < pages_.size(); ++i) if (pages_[i] <= selected_) *page = i;
        return S_OK;
    }
    STDMETHODIMP SetSelection(UINT index) override;
    STDMETHODIMP Finalize() override;
    STDMETHODIMP Abort() override;
};

class EditSession final : public RefCount, public ITfEditSession {
    Service* owner_;
    ComPtr<ITfContext> context_;
    Command command_;
    std::uint64_t epoch_;
public:
    EditSession(Service*, ITfContext*, Command, std::uint64_t);
    ~EditSession() override;
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfEditSession) return E_NOINTERFACE;
        *result = static_cast<ITfEditSession*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return Retain(); }
    STDMETHODIMP_(ULONG) Release() override { return Drop(); }
    STDMETHODIMP DoEditSession(TfEditCookie) override;
};

class Service final : public RefCount, public ITfTextInputProcessorEx, public ITfKeyEventSink,
                      public ITfThreadMgrEventSink, public ITfCompositionSink,
                      public ITfDisplayAttributeProvider, public ITfTextEditSink, public ITfTextLayoutSink,
                      public ITfCompartmentEventSink {
    struct CompartmentSubscription {
        ComPtr<ITfSource> source;
        DWORD cookie = kInvalidCookie;
    };
    std::vector<CompartmentSubscription> threadCompartments_;
    std::vector<CompartmentSubscription> contextCompartments_;
    ComPtr<ITfThreadMgr> thread_;
    ComPtr<ITfKeystrokeMgr> keys_;
    ComPtr<ITfSource> threadSource_;
    ComPtr<ITfContext> context_;
    ComPtr<ITfComposition> composition_;
    ComPtr<ITfSource> contextSource_;
    ComPtr<ITfUIElementMgr> uiManager_;
    ComPtr<CandidateElement> element_;
    ComPtr<ITfLangBarItemMgr> languageBar_;
    ComPtr<ModeButton> modeButton_;
    DWORD threadCookie_ = kInvalidCookie;
    DWORD contextCookie_ = kInvalidCookie;
    DWORD layoutCookie_ = kInvalidCookie;
    DWORD uiId_ = kInvalidCookie;
    TfClientId client_ = TF_CLIENTID_NULL;
    TfGuidAtom attributeAtom_ = TF_INVALID_GUIDATOM;
    std::uint64_t epoch_ = 1;
    bool keyAdvised_ = false;
    bool foreground_ = true;
    bool secure_ = false;
    bool uiOnly_ = false;
    bool finishing_ = false;
    bool changing_ = false;
    bool uiShown_ = true;
    bool dictionaryReady_ = false;
    bool refreshPending_ = false;
    bool discarding_ = false;
    std::array<bool, 256> consumed_{};
    Engine engine_;
    std::vector<Candidate> candidates_;
    size_t selected_ = 0;
    std::wstring original_;
    CandidateWindow window_;
    RECT lastCaret_{};
    HWND lastOwner_ = nullptr;
    bool hasCaret_ = false;

    HRESULT OpenForExplicitSelection() {
        if (!thread_ || client_ == TF_CLIENTID_NULL) return E_UNEXPECTED;
        ComPtr<ITfCompartmentMgr> manager;
        ComPtr<ITfCompartment> compartment;
        HRESULT hr = thread_->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(manager.put()));
        if (SUCCEEDED(hr)) hr = manager->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, compartment.put());
        if (FAILED(hr)) return hr;
        VARIANT value; VariantInit(&value); value.vt = VT_I4; value.lVal = TRUE;
        return compartment->SetValue(client_, &value);
    }

    void UnwatchCompartments(std::vector<CompartmentSubscription>& subscriptions) {
        // Move the list first so a reentrant notification cannot unadvise a
        // subscription twice. Sources keep their compartments alive until done.
        auto previous = std::move(subscriptions);
        subscriptions.clear();
        for (auto& subscription : previous)
            if (subscription.source && subscription.cookie != kInvalidCookie)
                subscription.source->UnadviseSink(subscription.cookie);
    }
    HRESULT WatchCompartments(IUnknown* owner, std::vector<CompartmentSubscription>& subscriptions) try {
        UnwatchCompartments(subscriptions);
        ComPtr<ITfCompartmentMgr> manager;
        HRESULT hr = owner->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(manager.put()));
        if (FAILED(hr)) return hr;
        subscriptions.reserve(3);
        for (const GUID* guid : {&GUID_COMPARTMENT_KEYBOARD_DISABLED, &GUID_COMPARTMENT_EMPTYCONTEXT,
                                 &GUID_COMPARTMENT_KEYBOARD_OPENCLOSE}) {
            ComPtr<ITfCompartment> compartment;
            CompartmentSubscription subscription;
            hr = manager->GetCompartment(*guid, compartment.put());
            if (SUCCEEDED(hr)) hr = compartment->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(subscription.source.put()));
            if (SUCCEEDED(hr)) hr = subscription.source->AdviseSink(IID_ITfCompartmentEventSink,
                static_cast<ITfCompartmentEventSink*>(this), &subscription.cookie);
            if (FAILED(hr)) { UnwatchCompartments(subscriptions); return hr; }
            subscriptions.push_back(std::move(subscription));
        }
        return S_OK;
    } catch (const std::bad_alloc&) { UnwatchCompartments(subscriptions); return E_OUTOFMEMORY; }
      catch (...) { UnwatchCompartments(subscriptions); return E_FAIL; }
    void DiscardForDisabledInput() {
        if (discarding_) return;
        struct Guard { bool& flag; explicit Guard(bool& value) : flag(value) { flag = true; }
                       ~Guard() { flag = false; } } guard(discarding_);
        window_.Hide();
        if (!composition_ || !context_) return;
        ComPtr<ITfContext> context(context_.get());
        ComPtr<ITfComposition> composition(composition_.get());
        auto session = new (std::nothrow) TerminationSession(context.get(), composition.get());
        if (session) {
            HRESULT result = E_FAIL;
            // A compartment notification may arrive inside another edit lock.
            // End only this old composition later; never replace text or move
            // the selection after the user has disabled input conversion.
            context->RequestEditSession(client_, session, TF_ES_ASYNC | TF_ES_READWRITE, &result);
            session->Release();
        }
        ClearState();
    }

    bool CompartmentsAllowed(ITfContext* context) const {
        if (!context || !thread_ || secure_ || !foreground_) return false;
        TF_STATUS status{};
        if (FAILED(context->GetStatus(&status)) || (status.dwDynamicFlags & TS_SD_READONLY)) return false;
        LONG value = 0;
        for (IUnknown* owner : {static_cast<IUnknown*>(context), static_cast<IUnknown*>(thread_.get())}) {
            if (ReadCompartment(owner, GUID_COMPARTMENT_KEYBOARD_DISABLED, value) && value) return false;
            if (ReadCompartment(owner, GUID_COMPARTMENT_EMPTYCONTEXT, value) && value) return false;
            if (ReadCompartment(owner, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, value) && !value) return false;
        }
        return true;
    }
    bool Allowed(ITfContext* context) const {
        if (!CompartmentsAllowed(context)) return false;
        auto session = new (std::nothrow) ScopeSession(context);
        if (!session) return false;
        HRESULT result = E_FAIL;
        HRESULT hr = context->RequestEditSession(client_, session, TF_ES_SYNC | TF_ES_READ, &result);
        bool allowed = SUCCEEDED(hr) && result == S_OK && session->allowed;
        session->Release(); return allowed;
    }
    Command Identify(WPARAM key, LPARAM parameter) const {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl && shift && !alt && key == 'E') return {Action::Toggle};
        if (!ctrl && !alt && !shift) {
            if (key == VK_DIVIDE) return {Action::Toggle};
            const unsigned scan = static_cast<unsigned>((parameter >> 16) & 0x1ff);
            const char digit = Engine::PhysicalToLogical(static_cast<unsigned>(key), scan);
            if (digit >= '2' && digit <= '9') return {Action::Digit, digit};
            if (digit == '1') return {Action::Comma};
            if (digit == '0') return {composition_ ? Action::Commit : Action::Space};
            if (digit == '.') return {Action::Period};
            if (composition_) {
                switch (key) {
                case VK_BACK: return {Action::Backspace};
                case VK_ESCAPE: return {Action::Cancel};
                case VK_RETURN: case VK_SPACE: return {Action::Commit};
                case VK_MULTIPLY: case VK_DOWN: case VK_RIGHT: return {Action::Next};
                case VK_UP: case VK_LEFT: return {Action::Previous};
                case VK_ADD: case VK_NEXT: return {Action::PageNext};
                case VK_SUBTRACT: case VK_PRIOR: return {Action::PagePrevious};
                default: break;
                }
            }
        }
        if (composition_ && key != VK_SHIFT && key != VK_CONTROL && key != VK_MENU && key != VK_LWIN && key != VK_RWIN)
            return {Action::CommitAndPass};
        return {};
    }
    HRESULT Start(ITfContext* context, TfEditCookie cookie) {
        TF_SELECTION selection{}; ULONG fetched = 0;
        HRESULT hr = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        if (FAILED(hr) || fetched != 1 || !selection.range) return FAILED(hr) ? hr : E_FAIL;
        ComPtr<ITfRange> range; range.reset(selection.range);
        hr = ReadOriginal(range.get(), cookie, original_); if (FAILED(hr)) return hr;
        ComPtr<ITfContextComposition> manager;
        hr = context->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(manager.put()));
        if (FAILED(hr)) return hr;
        hr = WatchCompartments(context, contextCompartments_);
        if (FAILED(hr)) return hr;
        ComPtr<ITfComposition> created;
        hr = manager->StartComposition(cookie, range.get(), static_cast<ITfCompositionSink*>(this), created.put());
        if (FAILED(hr) || !created) { UnwatchCompartments(contextCompartments_); return FAILED(hr) ? hr : E_FAIL; }
        context_ = ComPtr<ITfContext>(context); composition_ = std::move(created);
        context_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(contextSource_.put()));
        if (contextSource_) contextSource_->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this), &contextCookie_);
        if (contextSource_) contextSource_->AdviseSink(kIidTextLayoutSink, static_cast<ITfTextLayoutSink*>(this), &layoutCookie_);
        return S_OK;
    }
    bool SelectionAtEnd(TfEditCookie cookie) const {
        if (!composition_ || !context_) return false;
        ComPtr<ITfRange> range; if (FAILED(composition_->GetRange(range.put()))) return false;
        TF_SELECTION selection{}; ULONG fetched = 0;
        if (FAILED(context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) || fetched != 1 || !selection.range) return false;
        ComPtr<ITfRange> selected; selected.reset(selection.range);
        LONG start = 1, end = 1;
        return SUCCEEDED(selected->CompareStart(cookie, range.get(), TF_ANCHOR_END, &start)) &&
               SUCCEEDED(selected->CompareEnd(cookie, range.get(), TF_ANCHOR_END, &end)) && !start && !end;
    }
    HRESULT MoveCaret(TfEditCookie cookie, ITfRange* range) {
        ComPtr<ITfRange> caret; HRESULT hr = range->Clone(caret.put()); if (FAILED(hr)) return hr;
        hr = caret->Collapse(cookie, TF_ANCHOR_END); if (FAILED(hr)) return hr;
        TF_SELECTION selection{}; selection.range = caret.get(); selection.style.ase = TF_AE_NONE; selection.style.fInterimChar = FALSE;
        return context_->SetSelection(cookie, 1, &selection);
    }
    void ClearState() {
        ++epoch_;
        CloseUI();
        UnwatchCompartments(contextCompartments_);
        if (contextSource_ && contextCookie_ != kInvalidCookie) contextSource_->UnadviseSink(contextCookie_);
        if (contextSource_ && layoutCookie_ != kInvalidCookie) contextSource_->UnadviseSink(layoutCookie_);
        layoutCookie_ = kInvalidCookie; refreshPending_ = false;
        contextCookie_ = kInvalidCookie; contextSource_.reset();
        composition_.reset(); context_.reset(); engine_.Clear(); candidates_.clear(); selected_ = 0; original_.clear();
        hasCaret_ = false;
    }
    void CloseUI() {
        window_.Hide();
        if (element_) element_->Detach();
        const DWORD id = uiId_; uiId_ = kInvalidCookie;
        if (uiManager_ && id != kInvalidCookie) uiManager_->EndUIElement(id);
        element_.reset();
        uiShown_ = true;
    }
    void UpdateUI(TfEditCookie cookie) {
        if (!composition_ || !context_) { CloseUI(); return; }
        if (!CompartmentsAllowed(context_.get())) { window_.Hide(); return; }
        if (!element_ && uiManager_) {
            ComPtr<ITfDocumentMgr> document;
            if (SUCCEEDED(context_->GetDocumentMgr(document.put()))) {
                element_.reset(new (std::nothrow) CandidateElement(this, document.get()));
                if (element_) {
                    element_->Update(candidates_, selected_);
                    BOOL show = uiOnly_ ? FALSE : TRUE;
                    if (FAILED(uiManager_->BeginUIElement(element_.get(), &show, &uiId_))) { element_->Detach(); element_.reset(); uiId_ = kInvalidCookie; }
                    else { uiShown_ = show != FALSE; element_->Show(show); }
                }
            }
        }
        if (element_) {
            element_->Update(candidates_, selected_);
            if (uiId_ != kInvalidCookie) uiManager_->UpdateUIElement(uiId_);
        }
        ComPtr<ITfContextView> view; ComPtr<ITfRange> range;
        BOOL clipped = FALSE; RECT rect{}; HWND owner = nullptr;
        if (SUCCEEDED(context_->GetActiveView(view.put())) && SUCCEEDED(composition_->GetRange(range.put())) &&
            SUCCEEDED(range->Collapse(cookie, TF_ANCHOR_END)) && SUCCEEDED(view->GetTextExt(cookie, range.get(), &rect, &clipped))) {
            if (FAILED(view->GetWnd(&owner)) || !owner) owner = GetFocus();
            lastCaret_ = rect; lastOwner_ = owner; hasCaret_ = true;
            if (uiShown_ && !uiOnly_ && foreground_ && !clipped)
                window_.Update(rect, owner, engine_.GetMode(), engine_.Digits(), candidates_, selected_, dictionaryReady_);
            else window_.Hide();
        } else window_.Hide();
    }
    HRESULT Render(TfEditCookie cookie) {
        ComPtr<ITfRange> range; HRESULT hr = composition_->GetRange(range.put()); if (FAILED(hr)) return hr;
        std::wstring text(engine_.Digits().begin(), engine_.Digits().end());
        hr = range->SetText(cookie, 0, text.c_str(), static_cast<LONG>(text.size())); if (FAILED(hr)) return hr;
        ComPtr<ITfProperty> property;
        if (attributeAtom_ != TF_INVALID_GUIDATOM && SUCCEEDED(context_->GetProperty(GUID_PROP_ATTRIBUTE, property.put()))) {
            VARIANT value; VariantInit(&value); value.vt = VT_I4; value.lVal = static_cast<LONG>(attributeAtom_);
            property->SetValue(cookie, range.get(), &value);
        }
        hr = MoveCaret(cookie, range.get());
        candidates_ = engine_.Candidates(); selected_ = 0;
        UpdateUI(cookie); return hr;
    }
    HRESULT Finish(TfEditCookie cookie, bool cancel, const std::wstring& suffix, bool keepSelection) {
        if (!composition_ || !context_) return S_FALSE;
        ComPtr<ITfComposition> composition(composition_.get());
        ComPtr<ITfRange> range; HRESULT hr = composition->GetRange(range.put()); if (FAILED(hr)) return hr;
        std::wstring text = cancel ? original_ : (candidates_.empty() ? std::wstring(engine_.Digits().begin(), engine_.Digits().end()) : candidates_[std::min(selected_, candidates_.size() - 1)].text);
        text += suffix;
        hr = range->SetText(cookie, 0, text.c_str(), static_cast<LONG>(text.size())); if (FAILED(hr)) return hr;
        ComPtr<ITfProperty> property;
        if (SUCCEEDED(context_->GetProperty(GUID_PROP_ATTRIBUTE, property.put()))) property->Clear(cookie, range.get());
        if (!keepSelection) MoveCaret(cookie, range.get());
        finishing_ = true; hr = composition->EndComposition(cookie); finishing_ = false;
        if (FAILED(hr)) {
            auto session = new (std::nothrow) TerminationSession(context_.get(), composition.get());
            if (session) {
                HRESULT result = E_FAIL;
                context_->RequestEditSession(client_, session, TF_ES_ASYNC | TF_ES_READWRITE, &result);
                session->Release();
            }
        }
        ClearState();
        // Text has been delivered and must not be replaced by later keystrokes,
        // even if a buggy text store rejects synchronous EndComposition.
        return S_OK;
    }
    HRESULT InsertLiteral(ITfContext* context, TfEditCookie cookie, const std::wstring& text) {
        ComPtr<ITfInsertAtSelection> insertion;
        HRESULT hr = context->QueryInterface(IID_ITfInsertAtSelection, reinterpret_cast<void**>(insertion.put()));
        if (FAILED(hr)) return hr;
        ComPtr<ITfRange> range;
        hr = insertion->InsertTextAtSelection(cookie, 0, text.c_str(), static_cast<LONG>(text.size()), range.put());
        if (FAILED(hr)) return hr;
        ComPtr<ITfRange> caret; hr = range->Clone(caret.put()); if (FAILED(hr)) return S_OK;
        caret->Collapse(cookie, TF_ANCHOR_END);
        TF_SELECTION selection{}; selection.range = caret.get(); selection.style.ase = TF_AE_NONE;
        context->SetSelection(cookie, 1, &selection); return S_OK;
    }
    void EndForFocus() {
        window_.Hide();
        if (!composition_ || !context_) return;
        ComPtr<ITfContext> context(context_.get());
        Command command{Action::Commit}; command.keepSelection = true;
        HRESULT hr = Request(context.get(), command, false);
        if (FAILED(hr)) Request(context.get(), command, true);
    }
public:
    Service() {
        auto path = ModulePath(); auto separator = path.find_last_of(L"\\/");
        if (separator != std::wstring::npos) dictionaryReady_ = engine_.LoadDictionary(Utf8(path.substr(0, separator + 1) + L"lexicon.tsv"));
        window_.SetAction([this](size_t index, bool confirm) {
            Command select{Action::SetSelection}; select.index = static_cast<UINT>(index);
            const auto hr = CandidateCommand(select);
            if (SUCCEEDED(hr) && confirm) CandidateCommand({Action::Commit});
        });
    }
    ~Service() override { window_.Destroy(); }
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor || iid == kIidTipEx) *result = static_cast<ITfTextInputProcessorEx*>(this);
        else if (iid == IID_ITfKeyEventSink) *result = static_cast<ITfKeyEventSink*>(this);
        else if (iid == IID_ITfThreadMgrEventSink) *result = static_cast<ITfThreadMgrEventSink*>(this);
        else if (iid == IID_ITfCompositionSink) *result = static_cast<ITfCompositionSink*>(this);
        else if (iid == kIidDisplayAttributeProvider) *result = static_cast<ITfDisplayAttributeProvider*>(this);
        else if (iid == IID_ITfTextEditSink) *result = static_cast<ITfTextEditSink*>(this);
        else if (iid == kIidTextLayoutSink) *result = static_cast<ITfTextLayoutSink*>(this);
        else if (iid == IID_ITfCompartmentEventSink) *result = static_cast<ITfCompartmentEventSink*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return Retain(); }
    STDMETHODIMP_(ULONG) Release() override { return Drop(); }
    STDMETHODIMP Activate(ITfThreadMgr* thread, TfClientId client) override { return ActivateEx(thread, client, 0); }
    STDMETHODIMP ActivateEx(ITfThreadMgr* thread, TfClientId client, DWORD flags) override {
        if (!thread || client == TF_CLIENTID_NULL) return E_INVALIDARG;
        if (thread_) return E_UNEXPECTED;
        if (flags & TF_TMAE_SECUREMODE) return E_ACCESSDENIED;
        thread_ = ComPtr<ITfThreadMgr>(thread); client_ = client; secure_ = false; foreground_ = true;
        uiOnly_ = (flags & TF_TMAE_UIELEMENTENABLEDONLY) != 0;
        HRESULT hr = thread_->QueryInterface(IID_ITfKeystrokeMgr, reinterpret_cast<void**>(keys_.put()));
        if (SUCCEEDED(hr)) hr = keys_->AdviseKeyEventSink(client_, static_cast<ITfKeyEventSink*>(this), TRUE);
        if (SUCCEEDED(hr)) keyAdvised_ = true;
        if (SUCCEEDED(hr)) hr = thread_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(threadSource_.put()));
        if (SUCCEEDED(hr)) hr = threadSource_->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &threadCookie_);
        if (SUCCEEDED(hr)) hr = WatchCompartments(thread_.get(), threadCompartments_);
        thread_->QueryInterface(kIidUiElementMgr, reinterpret_cast<void**>(uiManager_.put()));
        if (uiOnly_ && !uiManager_) hr = E_NOINTERFACE;
        if (FAILED(hr)) { Deactivate(); return hr; }
        if (!uiOnly_ && SUCCEEDED(thread_->QueryInterface(IID_ITfLangBarItemMgr, reinterpret_cast<void**>(languageBar_.put())))) {
            modeButton_.reset(new (std::nothrow) ModeButton([this]() -> HRESULT {
                // Clicking our mode button is an explicit request to use this
                // conversion mode. Ordinary keyboard events never open the IME.
                const HRESULT open = OpenForExplicitSelection();
                if (FAILED(open)) return open;
                if (composition_) return CandidateCommand({Action::Toggle});
                engine_.SetMode(engine_.GetMode() == Mode::Pinyin ? Mode::English : Mode::Pinyin);
                if (modeButton_) modeButton_->SetMode(engine_.GetMode());
                return S_OK;
            }));
            if (modeButton_) {
                modeButton_->SetMode(engine_.GetMode());
                if (FAILED(languageBar_->AddItem(modeButton_.get()))) { modeButton_->Detach(); modeButton_.reset(); }
            }
        }
        ComPtr<ITfCategoryMgr> categories;
        if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(categories.put()))))
            categories->RegisterGUID(kDisplayAttribute, &attributeAtom_);
        // Windows may retain a closed state from the previous input profile.
        // Initialize this newly activated IME as open, as Microsoft's SampleIME
        // InitializeSampleIMECompartment does; later user close events remain
        // authoritative and preserve normal key pass-through.
        hr = OpenForExplicitSelection();
        if (FAILED(hr)) { Deactivate(); return hr; }
        return S_OK;
    }
    STDMETHODIMP Deactivate() override {
        if (composition_ && context_) {
            ComPtr<ITfContext> context(context_.get());
            Command command{Action::Commit}; command.keepSelection = true;
            Request(context.get(), command, false);
            if (composition_) {
                auto session = new (std::nothrow) TerminationSession(context.get(), composition_.get());
                if (session) {
                    HRESULT result = E_FAIL;
                    context->RequestEditSession(client_, session, TF_ES_ASYNC | TF_ES_READWRITE, &result);
                    session->Release();
                }
                // The old TSF composition may temporarily retain the sink, but
                // the service retains neither context nor composition: no cycle.
                // If cleanup cannot run, visible raw preedit is preserved.
                ClearState();
            }
        }
        if (keyAdvised_ && keys_) keys_->UnadviseKeyEventSink(client_);
        keyAdvised_ = false;
        if (threadSource_ && threadCookie_ != kInvalidCookie) threadSource_->UnadviseSink(threadCookie_);
        threadCookie_ = kInvalidCookie;
        UnwatchCompartments(contextCompartments_);
        UnwatchCompartments(threadCompartments_);
        CloseUI(); window_.Destroy();
        if (modeButton_) {
            modeButton_->Detach();
            if (languageBar_) languageBar_->RemoveItem(modeButton_.get());
        }
        modeButton_.reset(); languageBar_.reset();
        // OPENCLOSE is a shared Windows input-mode compartment. SampleIME's
        // _ClearCompartment explicitly leaves it intact; do not clear it or
        // restore an older profile's value over the user's latest selection.
        keys_.reset(); threadSource_.reset(); uiManager_.reset(); thread_.reset(); consumed_.fill(false);
        client_ = TF_CLIENTID_NULL; foreground_ = false;
        // An already scheduled cleanup session holds this service/context alive. Its
        // epoch check prevents it from modifying any later composition.
        return S_OK;
    }
    STDMETHODIMP OnSetFocus(BOOL foreground) override { foreground_ = foreground != FALSE; if (!foreground_) EndForFocus(); return S_OK; }
    STDMETHODIMP OnChange(REFGUID) override {
        // Unadvising old-context subscriptions may release this sink while its
        // callback is still on the stack; retain it until cleanup completes.
        ComPtr<ITfCompartmentEventSink> keepAlive(static_cast<ITfCompartmentEventSink*>(this));
        try {
            if (composition_ && context_ && !CompartmentsAllowed(context_.get())) {
                window_.Hide();
                // A host can change a compartment reentrantly during SetText.
                // Do not clear pointers that its current edit session still uses.
                if (changing_ || finishing_) return Request(context_.get(), {Action::DiscardDisabled}, true);
                DiscardForDisabledInput();
            }
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    }
    STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM parameter, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        try { if (Identify(key, parameter).action != Action::None && Allowed(context)) *eaten = TRUE; } catch (...) { *eaten = FALSE; }
        return S_OK;
    }
    STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM key, LPARAM parameter, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = FALSE;
        try {
            auto command = Identify(key, parameter);
            if (command.action == Action::None || !Allowed(context)) return S_OK;
            const HRESULT hr = Request(context, command, false);
            if (hr == S_OK && command.action != Action::CommitAndPass) {
                *eaten = TRUE; if (key < consumed_.size()) consumed_[key] = true;
            }
        } catch (...) { *eaten = FALSE; }
        return S_OK;
    }
    STDMETHODIMP OnTestKeyUp(ITfContext*, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = key < consumed_.size() && consumed_[key] ? TRUE : FALSE; return S_OK;
    }
    STDMETHODIMP OnKeyUp(ITfContext*, WPARAM key, LPARAM, BOOL* eaten) override {
        if (!eaten) return E_POINTER;
        *eaten = key < consumed_.size() && consumed_[key] ? TRUE : FALSE;
        if (key < consumed_.size()) consumed_[key] = false;
        return S_OK;
    }
    STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override { if (!eaten) return E_POINTER;
        *eaten = FALSE; return S_OK; }
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* document) override {
        if (context_) { ComPtr<ITfDocumentMgr> current; context_->GetDocumentMgr(current.put()); if (current.get() == document) EndForFocus(); }
        return S_OK;
    }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* document, ITfDocumentMgr*) override {
        if (context_) { ComPtr<ITfDocumentMgr> current; context_->GetDocumentMgr(current.put()); if (current.get() != document) EndForFocus(); }
        return S_OK;
    }
    STDMETHODIMP OnPushContext(ITfContext* context) override { if (context_ && context_.get() != context) EndForFocus(); return S_OK; }
    STDMETHODIMP OnPopContext(ITfContext* context) override { if (context_.get() == context) EndForFocus(); return S_OK; }
    STDMETHODIMP OnCompositionTerminated(TfEditCookie cookie, ITfComposition* composition) override {
        if (composition_.get() != composition || finishing_) return S_OK;
        // External termination keeps the user's visible preedit. Never delete a
        // range after TSF has handed ownership of it back to the application.
        ComPtr<ITfRange> range; ComPtr<ITfProperty> property;
        if (context_ && SUCCEEDED(composition->GetRange(range.put())) && SUCCEEDED(context_->GetProperty(GUID_PROP_ATTRIBUTE, property.put()))) property->Clear(cookie, range.get());
        ClearState(); return S_OK;
    }
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** result) override { return CreateAttributeEnumerator(result); }
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return guid == kDisplayAttribute ? CreateAttributeInfo(result) : E_INVALIDARG;
    }
    STDMETHODIMP OnEndEdit(ITfContext* context, TfEditCookie cookie, ITfEditRecord* record) override {
        if (!composition_ || context_.get() != context || finishing_ || changing_) return S_OK;
        BOOL changed = FALSE;
        if (record && SUCCEEDED(record->GetSelectionStatus(&changed)) && changed && !SelectionAtEnd(cookie)) {
            window_.Hide(); Command command{Action::Commit}; command.keepSelection = true;
            Request(context, command, true);
        }
        return S_OK;
    }
    STDMETHODIMP OnLayoutChange(ITfContext* context, TfLayoutCode code, ITfContextView*) override {
        if (context_.get() != context || !composition_ || changing_ || finishing_) return S_OK;
        if (code == TF_LC_DESTROY) { window_.Hide(); return S_OK; }
        if (!refreshPending_) {
            refreshPending_ = true;
            if (FAILED(Request(context, {Action::Refresh}, true))) refreshPending_ = false;
        }
        return S_OK;
    }
    HRESULT Request(ITfContext* context, Command command, bool asynchronous) {
        if (!context || client_ == TF_CLIENTID_NULL) return E_UNEXPECTED;
        auto session = new (std::nothrow) EditSession(this, context, command, epoch_);
        if (!session) return E_OUTOFMEMORY;
        HRESULT result = E_FAIL;
        const HRESULT hr = context->RequestEditSession(client_, session,
            (asynchronous ? TF_ES_ASYNC : TF_ES_SYNC) |
            ((command.action == Action::Refresh || command.action == Action::DiscardDisabled) ? TF_ES_READ : TF_ES_READWRITE), &result);
        session->Release(); return FAILED(hr) ? hr : result;
    }
    HRESULT Apply(ITfContext* context, TfEditCookie cookie, Command command, std::uint64_t epoch) {
        if (epoch != epoch_) return S_FALSE;
        if (command.action == Action::DiscardDisabled) {
            if (composition_ && context_.get() == context && !CompartmentsAllowed(context)) DiscardForDisabledInput();
            return S_OK;
        }
        if (command.action == Action::Refresh) {
            refreshPending_ = false;
            if (composition_ && context_.get() == context && CompartmentsAllowed(context) && ScopeAllowed(context, cookie)) UpdateUI(cookie);
            else window_.Hide();
            return S_OK;
        }
        if (command.action == Action::Commit && command.keepSelection) {
            changing_ = true; auto result = Finish(cookie, false, {}, true); changing_ = false; return result;
        }
        if (!CompartmentsAllowed(context) || !ScopeAllowed(context, cookie)) return E_ACCESSDENIED;
        if (context_ && context_.get() != context) return E_UNEXPECTED;
        if (composition_ && !SelectionAtEnd(cookie)) return E_UNEXPECTED;
        struct ChangeGuard { bool& value; explicit ChangeGuard(bool& v) : value(v) { value = true; } ~ChangeGuard() { value = false; } } guard(changing_);
        if (command.action == Action::Digit || command.action == Action::Backspace) {
            const auto previous = engine_.Digits();
            if (command.action == Action::Digit) {
                if (!dictionaryReady_ || !engine_.AppendDigit(command.digit)) return S_FALSE;
                if (!composition_) {
                    const auto hr = Start(context, cookie);
                    if (FAILED(hr)) { engine_.Clear(); return hr; }
                }
            } else {
                if (!composition_) return S_FALSE;
                engine_.Backspace();
                if (engine_.Digits().empty()) return Finish(cookie, true, {}, false);
            }
            const auto hr = Render(cookie);
            if (FAILED(hr)) {
                engine_.Clear(); for (char digit : previous) engine_.AppendDigit(digit);
                if (previous.empty()) Finish(cookie, true, {}, false);
            }
            return hr;
        }
        if (command.action == Action::Cancel) return Finish(cookie, true, {}, command.keepSelection);
        if (command.action == Action::Commit || command.action == Action::CommitAndPass) return Finish(cookie, false, {}, command.keepSelection);
        if (command.action == Action::Toggle) {
            if (composition_) { const auto hr = Finish(cookie, false, {}, false); if (FAILED(hr)) return hr;
        }
            engine_.SetMode(engine_.GetMode() == Mode::Pinyin ? Mode::English : Mode::Pinyin);
            if (modeButton_) modeButton_->SetMode(engine_.GetMode());
            return S_OK;
        }
        if (command.action == Action::Space || command.action == Action::Comma || command.action == Action::Period) {
            const auto text = command.action == Action::Space ? L" " :
                (command.action == Action::Comma ? (engine_.GetMode() == Mode::Pinyin ? L"，" : L",") : (engine_.GetMode() == Mode::Pinyin ? L"。" : L"."));
            return composition_ ? Finish(cookie, false, text, false) : InsertLiteral(context, cookie, text);
        }
        if (!composition_) return S_FALSE;
        if (!candidates_.empty()) {
            if (command.action == Action::Next) selected_ = (selected_ + 1) % candidates_.size();
            if (command.action == Action::Previous) selected_ = (selected_ + candidates_.size() - 1) % candidates_.size();
            if (command.action == Action::PageNext) selected_ = std::min((selected_ / 9 + 1) * 9, candidates_.size() - 1);
            if (command.action == Action::PagePrevious) selected_ = selected_ >= 9 ? (selected_ / 9 - 1) * 9 : 0;
            if (command.action == Action::SetSelection) { if (command.index >= candidates_.size()) return E_INVALIDARG;
        selected_ = command.index; }
        }
        UpdateUI(cookie); return S_OK;
    }
    void ShowUI(bool show) {
        uiShown_ = show;
        if (!show || uiOnly_ || !foreground_ || !composition_ || !hasCaret_) window_.Hide();
        else window_.Update(lastCaret_, lastOwner_, engine_.GetMode(), engine_.Digits(), candidates_, selected_, dictionaryReady_);
    }
    HRESULT CandidateCommand(Command command) {
        if (!context_ || !composition_) return E_UNEXPECTED;
        ComPtr<ITfContext> context(context_.get());
        auto hr = Request(context.get(), command, false);
        if (hr == TF_E_SYNCHRONOUS || hr == TF_E_LOCKED) hr = Request(context.get(), command, true);
        return hr;
    }
};

EditSession::EditSession(Service* service, ITfContext* context, Command command, std::uint64_t epoch)
    : owner_(service), context_(context), command_(command), epoch_(epoch) { owner_->AddRef(); }
EditSession::~EditSession() { owner_->Release(); }
HRESULT EditSession::DoEditSession(TfEditCookie cookie) {
    try { return owner_->Apply(context_.get(), cookie, command_, epoch_); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
HRESULT CandidateElement::Show(BOOL show) {
    try { shown_ = show; if (owner_) owner_->ShowUI(show != FALSE); return S_OK; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
HRESULT CandidateElement::SetSelection(UINT index) {
    if (!owner_ || index >= candidates_.size()) return E_INVALIDARG;
    Command command{Action::SetSelection}; command.index = index; return owner_->CandidateCommand(command);
}
HRESULT CandidateElement::Finalize() { return owner_ ? owner_->CandidateCommand({Action::Commit}) : E_UNEXPECTED; }
HRESULT CandidateElement::Abort() { return owner_ ? owner_->CandidateCommand({Action::Cancel}) : E_UNEXPECTED; }

class ClassFactory final : public RefCount, public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *result = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return Retain(); }
    STDMETHODIMP_(ULONG) Release() override { return Drop(); }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        try {
            auto service = new Service(); const HRESULT hr = service->QueryInterface(iid, result); service->Release(); return hr;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    }
    STDMETHODIMP LockServer(BOOL lock) override { if (lock) ++gServerLocks; else if (gServerLocks.load() > 0) --gServerLocks; return S_OK; }
};
}
}

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) numpad9::ime::gModule = module;
    return TRUE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** result) {
    using namespace numpad9::ime;
    if (!result) return E_POINTER;
        *result = nullptr;
    if (clsid != kClsid) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new (std::nothrow) ClassFactory(); if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, result); factory->Release(); return hr;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() {
    return !numpad9::ime::gObjects.load() && !numpad9::ime::gServerLocks.load() ? S_OK : S_FALSE;
}
