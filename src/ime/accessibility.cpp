#include "accessibility.h"
#include "com.h"
#ifndef __MINGW32__
#include <UIAutomation.h>
#else
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>
// MinGW 14's client header references newer enums absent from its core header.
// Use the SDK's stable numeric UIA identifiers without pulling in unused clients.
constexpr int UIA_InvokePatternId = 10000;
constexpr int UIA_SelectionPatternId = 10001;
constexpr int UIA_SelectionItemPatternId = 10010;
constexpr int UIA_MenuOpenedEventId = 20003;
constexpr int UIA_MenuClosedEventId = 20007;
constexpr int UIA_SelectionItem_ElementSelectedEventId = 20012;
constexpr int UIA_ControlTypePropertyId = 30003;
constexpr int UIA_NamePropertyId = 30005;
constexpr int UIA_HasKeyboardFocusPropertyId = 30008;
constexpr int UIA_IsKeyboardFocusablePropertyId = 30009;
constexpr int UIA_IsEnabledPropertyId = 30010;
constexpr int UIA_AutomationIdPropertyId = 30011;
constexpr int UIA_HelpTextPropertyId = 30013;
constexpr int UIA_IsControlElementPropertyId = 30016;
constexpr int UIA_IsContentElementPropertyId = 30017;
constexpr int UIA_IsOffscreenPropertyId = 30022;
constexpr int UIA_SelectionItemIsSelectedPropertyId = 30079;
constexpr int UIA_ListItemControlTypeId = 50007;
constexpr int UIA_ListControlTypeId = 50008;
#endif
#include <oleauto.h>
#include <algorithm>
#include <mutex>
#include <new>

namespace numpad9::ime {
namespace {
struct State {
    std::mutex mutex;
    HWND hwnd = nullptr;
    std::vector<Candidate> candidates;
    size_t selected = 0;
    UINT dpi = 96;
    bool visible = false;
    UINT generation = 1;
};
struct View {
    HWND hwnd; std::vector<Candidate> candidates; size_t selected; UINT dpi; bool visible; UINT generation;
    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    bool Valid(int index) const { return hwnd && (index < 0 || static_cast<size_t>(index) < candidates.size()); }
    bool OnPage(int index) const { return visible && index >= 0 && static_cast<size_t>(index) / 9 == selected / 9; }
    RECT Bounds(int index) const {
        RECT rect{}; if (!hwnd || !GetWindowRect(hwnd, &rect)) return {};
        if (index < 0) return rect;
        if (!OnPage(index)) return {};
        const int top = rect.top + Scale(44 + (index % 9) * 31);
        return {rect.left + Scale(8), top, rect.right - Scale(8), top + Scale(30)};
    }
};
class Provider final : public RefCount, public IRawElementProviderSimple, public IRawElementProviderFragment,
                       public IRawElementProviderFragmentRoot, public ISelectionProvider,
                       public ISelectionItemProvider, public IInvokeProvider {
    std::shared_ptr<State> state_;
    ComPtr<Provider> root_;
    int index_;
    UINT generation_;
    View Snapshot() const {
        std::lock_guard<std::mutex> guard(state_->mutex);
        return {state_->hwnd, state_->candidates, state_->selected, state_->dpi, state_->visible, state_->generation};
    }
    bool Valid(const View& view) const { return view.Valid(index_) && (index_ < 0 || generation_ == view.generation); }
    Provider* Root() { return index_ < 0 ? this : root_.get(); }
    HRESULT Fragment(int index, IRawElementProviderFragment** result) {
        if (!result) return E_POINTER;
        *result = nullptr;
        const auto view = Snapshot();
        if (!Valid(view)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (index < 0) { *result = static_cast<IRawElementProviderFragment*>(Root()); (*result)->AddRef(); return S_OK; }
        if (static_cast<size_t>(index) >= view.candidates.size()) return S_OK;
        auto child = new (std::nothrow) Provider(state_, index, view.generation, Root());
        if (!child) return E_OUTOFMEMORY;
        *result = static_cast<IRawElementProviderFragment*>(child); return S_OK;
    }
    HRESULT Send(bool confirm) {
        const auto view = Snapshot(); if (!Valid(view) || index_ < 0) return UIA_E_ELEMENTNOTAVAILABLE;
        const WPARAM action = static_cast<WPARAM>(index_) | (confirm ? 0x10000u : 0u);
        return PostMessageW(view.hwnd, kCandidateActionMessage, action, view.generation) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
public:
    Provider(std::shared_ptr<State> state, int index, UINT generation, Provider* root = nullptr)
        : state_(std::move(state)), root_(root), index_(index), generation_(generation) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override try {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_IRawElementProviderSimple) *result = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == IID_IRawElementProviderFragment) *result = static_cast<IRawElementProviderFragment*>(this);
        else if (iid == IID_IRawElementProviderFragmentRoot && index_ < 0) *result = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (iid == IID_ISelectionProvider && index_ < 0) *result = static_cast<ISelectionProvider*>(this);
        else if (iid == IID_ISelectionItemProvider && index_ >= 0) *result = static_cast<ISelectionItemProvider*>(this);
        else if (iid == IID_IInvokeProvider && index_ >= 0) *result = static_cast<IInvokeProvider*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP_(ULONG) AddRef() override { return Retain(); }
    STDMETHODIMP_(ULONG) Release() override { return Drop(); }
    STDMETHODIMP get_ProviderOptions(ProviderOptions* value) override try {
        if (!value) return E_POINTER;
        *value = ProviderOptions_ServerSideProvider; return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP GetPatternProvider(PATTERNID pattern, IUnknown** result) override try {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (index_ < 0 && pattern == UIA_SelectionPatternId) return QueryInterface(IID_ISelectionProvider, reinterpret_cast<void**>(result));
        if (index_ >= 0 && pattern == UIA_SelectionItemPatternId) return QueryInterface(IID_ISelectionItemProvider, reinterpret_cast<void**>(result));
        if (index_ >= 0 && pattern == UIA_InvokePatternId) return QueryInterface(IID_IInvokeProvider, reinterpret_cast<void**>(result));
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP GetPropertyValue(PROPERTYID property, VARIANT* value) override try {
        if (!value) return E_POINTER;
        VariantInit(value);
        const auto view = Snapshot(); if (!Valid(view)) return UIA_E_ELEMENTNOTAVAILABLE;
        auto boolean = [value](bool state) { value->vt = VT_BOOL; value->boolVal = state ? VARIANT_TRUE : VARIANT_FALSE; };
        switch (property) {
        case UIA_ControlTypePropertyId: value->vt = VT_I4; value->lVal = index_ < 0 ? UIA_ListControlTypeId : UIA_ListItemControlTypeId; break;
        case UIA_NamePropertyId:
            value->vt = VT_BSTR; value->bstrVal = SysAllocString(index_ < 0 ? L"Numpad9 候选词" : view.candidates[index_].text.c_str());
            if (!value->bstrVal) return E_OUTOFMEMORY;
        break;
        case UIA_AutomationIdPropertyId:
            value->vt = VT_BSTR; value->bstrVal = SysAllocString(index_ < 0 ? L"IME_Candidate_Window" : (L"candidate_" + std::to_wstring(index_)).c_str());
            if (!value->bstrVal) return E_OUTOFMEMORY;
        break;
        case UIA_HelpTextPropertyId:
            if (index_ >= 0) {
                const auto& spelling = view.candidates[index_].spelling;
                std::wstring text(spelling.begin(), spelling.end()); value->vt = VT_BSTR; value->bstrVal = SysAllocString(text.c_str());
                if (!value->bstrVal) return E_OUTOFMEMORY;
            } break;
        case UIA_IsEnabledPropertyId: boolean(true); break;
        case UIA_IsControlElementPropertyId: case UIA_IsContentElementPropertyId: boolean(true); break;
        case UIA_IsKeyboardFocusablePropertyId: boolean(false); break;
        case UIA_HasKeyboardFocusPropertyId: boolean(false); break;
        case UIA_IsOffscreenPropertyId: boolean(!view.visible || (index_ >= 0 && !view.OnPage(index_))); break;
        case UIA_SelectionItemIsSelectedPropertyId: if (index_ >= 0) boolean(static_cast<size_t>(index_) == view.selected); break;
        default: break;
        }
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** result) override try {
        if (!result) return E_POINTER;
        *result = nullptr;
        const auto view = Snapshot(); if (!Valid(view)) return UIA_E_ELEMENTNOTAVAILABLE;
        return index_ < 0 ? UiaHostProviderFromHwnd(view.hwnd, result) : S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override try {
        if (!result) return E_POINTER;
        *result = nullptr;
        const auto view = Snapshot(); if (!Valid(view)) return UIA_E_ELEMENTNOTAVAILABLE;
        const int first = static_cast<int>(view.selected / 9 * 9);
        const int last = static_cast<int>(std::min(view.candidates.size(), static_cast<size_t>(first + 9))) - 1;
        if (index_ < 0) {
            if (view.candidates.empty()) return S_OK;
            if (direction == NavigateDirection_FirstChild) return Fragment(first, result);
            if (direction == NavigateDirection_LastChild) return Fragment(last, result);
        } else {
            if (direction == NavigateDirection_Parent) return Fragment(-1, result);
            if (direction == NavigateDirection_NextSibling && index_ < last) return Fragment(index_ + 1, result);
            if (direction == NavigateDirection_PreviousSibling && index_ > first) return Fragment(index_ - 1, result);
        }
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP GetRuntimeId(SAFEARRAY** result) override try {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (index_ < 0) return S_OK;
        // Root identity comes from the host HWND.
        const auto view = Snapshot(); if (!Valid(view)) return UIA_E_ELEMENTNOTAVAILABLE;
        int values[] = {UiaAppendRuntimeId, static_cast<int>(view.generation), index_};
        *result = SafeArrayCreateVector(VT_I4, 0, 3); if (!*result) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 3; ++i) SafeArrayPutElement(*result, &i, &values[i]);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP get_BoundingRectangle(UiaRect* result) override try {
        if (!result) return E_POINTER;
        const auto view = Snapshot(); if (!Valid(view)) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto rect = view.Bounds(index_);
        *result = {static_cast<double>(rect.left), static_cast<double>(rect.top), static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top)};
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** result) override try { if (!result) return E_POINTER;
        *result = nullptr; return S_OK; } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP SetFocus() override try { return index_ >= 0 ? Send(false) : S_OK; } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** result) override try {
        if (!result) return E_POINTER;
        *result = static_cast<IRawElementProviderFragmentRoot*>(Root()); (*result)->AddRef(); return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** result) override try {
        if (!result) return E_POINTER;
        *result = nullptr;
        const auto view = Snapshot(); if (!Valid(view)) return UIA_E_ELEMENTNOTAVAILABLE;
        POINT point{static_cast<LONG>(x), static_cast<LONG>(y)};
        const auto bounds = view.Bounds(-1); if (!PtInRect(&bounds, point)) return S_OK;
        for (size_t i = view.selected / 9 * 9; i < std::min(view.candidates.size(), (view.selected / 9 + 1) * 9); ++i) {
            const auto row = view.Bounds(static_cast<int>(i)); if (PtInRect(&row, point)) return Fragment(static_cast<int>(i), result);
        }
        return Fragment(-1, result);
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP GetFocus(IRawElementProviderFragment** result) override try { if (!result) return E_POINTER;
        *result = nullptr; return S_OK; } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP GetSelection(SAFEARRAY** result) override try {
        if (!result) return E_POINTER;
        *result = nullptr;
        const auto view = Snapshot(); if (!Valid(view)) return UIA_E_ELEMENTNOTAVAILABLE;
        *result = SafeArrayCreateVector(VT_UNKNOWN, 0, view.candidates.empty() ? 0 : 1); if (!*result) return E_OUTOFMEMORY;
        if (!view.candidates.empty()) {
            ComPtr<IRawElementProviderFragment> selected; auto hr = Fragment(static_cast<int>(view.selected), selected.put()); if (FAILED(hr)) return hr;
            ComPtr<IRawElementProviderSimple> simple; selected->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(simple.put()));
            LONG index = 0; SafeArrayPutElement(*result, &index, simple.get());
        }
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP get_CanSelectMultiple(BOOL* value) override try { if (!value) return E_POINTER;
        *value = FALSE; return S_OK; } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP get_IsSelectionRequired(BOOL* value) override try { if (!value) return E_POINTER;
        *value = TRUE; return S_OK; } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP Select() override try { return Send(false); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP AddToSelection() override try { return Send(false); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP RemoveFromSelection() override try { return UIA_E_INVALIDOPERATION; } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP get_IsSelected(BOOL* value) override try {
        if (!value) return E_POINTER;
        const auto view = Snapshot(); if (!Valid(view)) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = index_ >= 0 && static_cast<size_t>(index_) == view.selected ? TRUE : FALSE; return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP get_SelectionContainer(IRawElementProviderSimple** result) override try {
        if (!result) return E_POINTER;
        *result = static_cast<IRawElementProviderSimple*>(Root()); (*result)->AddRef(); return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    STDMETHODIMP Invoke() override try { return Send(true); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    void SelectionEvent() {
        const auto view = Snapshot(); if (view.candidates.empty()) return;
        ComPtr<IRawElementProviderFragment> child;
        if (SUCCEEDED(Fragment(static_cast<int>(view.selected), child.put()))) {
            ComPtr<IRawElementProviderSimple> simple;
            if (SUCCEEDED(child->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(simple.put()))))
                UiaRaiseAutomationEvent(simple.get(), UIA_SelectionItem_ElementSelectedEventId);
        }
    }
};
}
struct CandidateAccessibility::Impl {
    std::shared_ptr<State> state = std::make_shared<State>();
    ComPtr<Provider> provider;
};
CandidateAccessibility::CandidateAccessibility() : impl_(std::make_unique<Impl>()) {}
CandidateAccessibility::~CandidateAccessibility() { Detach(); }
void CandidateAccessibility::Attach(HWND hwnd) {
    Detach();
    { std::lock_guard<std::mutex> guard(impl_->state->mutex); impl_->state->hwnd = hwnd; }
    impl_->provider.reset(new (std::nothrow) Provider(impl_->state, -1, 0));
}
void CandidateAccessibility::Update(const std::vector<Candidate>& candidates, size_t selected, UINT dpi, bool visible) {
    bool appeared = false, selectionChanged = false;
    {
        std::lock_guard<std::mutex> guard(impl_->state->mutex);
        auto& state = *impl_->state;
        bool listChanged = state.candidates.size() != candidates.size();
        if (!listChanged) for (size_t i = 0; i < candidates.size(); ++i) if (state.candidates[i].text != candidates[i].text) { listChanged = true; break; }
        if (listChanged) ++state.generation;
        appeared = visible && !state.visible;
        selectionChanged = appeared || listChanged || selected != state.selected;
        state.candidates = candidates; state.selected = selected; state.dpi = dpi; state.visible = visible;
    }
    if (impl_->provider) {
        if (appeared) UiaRaiseAutomationEvent(impl_->provider.get(), UIA_MenuOpenedEventId);
        if (selectionChanged && visible) impl_->provider->SelectionEvent();
    }
}
void CandidateAccessibility::Hide() {
    bool hidden = false;
    { std::lock_guard<std::mutex> guard(impl_->state->mutex); hidden = impl_->state->visible; impl_->state->visible = false; }
    if (hidden && impl_->provider) UiaRaiseAutomationEvent(impl_->provider.get(), UIA_MenuClosedEventId);
}
void CandidateAccessibility::Detach() {
    Hide();
    { std::lock_guard<std::mutex> guard(impl_->state->mutex); impl_->state->hwnd = nullptr; ++impl_->state->generation; }
    impl_->provider.reset();
}
LRESULT CandidateAccessibility::GetObject(WPARAM wParam, LPARAM lParam) {
    if (lParam != UiaRootObjectId || !impl_->provider) return 0;
    HWND hwnd;
    { std::lock_guard<std::mutex> guard(impl_->state->mutex); hwnd = impl_->state->hwnd; }
    return UiaReturnRawElementProvider(hwnd, wParam, lParam, impl_->provider.get());
}
bool CandidateAccessibility::IsCurrentGeneration(LPARAM generation) const {
    std::lock_guard<std::mutex> guard(impl_->state->mutex); return static_cast<UINT>(generation) == impl_->state->generation;
}
}
