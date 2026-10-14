#include "display_attribute.h"
#include "com.h"
#include "identity.h"
#include <new>
#include <oleauto.h>

namespace numpad9::ime {
namespace {
TF_DISPLAYATTRIBUTE DefaultAttribute() {
    TF_DISPLAYATTRIBUTE attribute{};
    attribute.crText.type = TF_CT_NONE; attribute.crBk.type = TF_CT_NONE;
    attribute.lsStyle = TF_LS_DOT; attribute.fBoldLine = FALSE;
    attribute.crLine.type = TF_CT_SYSCOLOR; attribute.crLine.nIndex = COLOR_WINDOWTEXT;
    attribute.bAttr = TF_ATTR_INPUT;
    return attribute;
}
class Attribute final : public RefCount, public ITfDisplayAttributeInfo {
    TF_DISPLAYATTRIBUTE value_ = DefaultAttribute();
public:
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfDisplayAttributeInfo) *result = static_cast<ITfDisplayAttributeInfo*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return Retain(); }
    STDMETHODIMP_(ULONG) Release() override { return Drop(); }
    STDMETHODIMP GetGUID(GUID* guid) override { if (!guid) return E_POINTER;
        *guid = kDisplayAttribute; return S_OK; }
    STDMETHODIMP GetDescription(BSTR* text) override { if (!text) return E_POINTER;
        *text = SysAllocString(L"Numpad9 unconverted input"); return *text ? S_OK : E_OUTOFMEMORY; }
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* value) override { if (!value) return E_POINTER;
        *value = value_; return S_OK; }
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE* value) override { if (!value) return E_POINTER;
        value_ = *value; return S_OK; }
    STDMETHODIMP Reset() override { value_ = DefaultAttribute(); return S_OK; }
};
class Enumerator final : public RefCount, public IEnumTfDisplayAttributeInfo {
    ULONG index_ = 0;
public:
    explicit Enumerator(ULONG index = 0) : index_(index) {}
    STDMETHODIMP QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == IID_IUnknown || iid == IID_IEnumTfDisplayAttributeInfo) *result = static_cast<IEnumTfDisplayAttributeInfo*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return Retain(); }
    STDMETHODIMP_(ULONG) Release() override { return Drop(); }
    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** result) override {
        if (!result) return E_POINTER;
        *result = new (std::nothrow) Enumerator(index_); return *result ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo** result, ULONG* fetched) override {
        if (!result || (!fetched && count != 1)) return E_POINTER;
        if (fetched) *fetched = 0;
        if (!count) return S_OK;
        if (index_) return S_FALSE;
        HRESULT hr = CreateAttributeInfo(&result[0]);
        if (FAILED(hr)) return hr;
        index_ = 1; if (fetched) *fetched = 1;
        return count == 1 ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override { index_ = 0; return S_OK; }
    STDMETHODIMP Skip(ULONG count) override {
        if (!count) return S_OK;
        auto old = index_; index_ = 1;
        return !old && count == 1 ? S_OK : S_FALSE;
    }
};
}
HRESULT CreateAttributeInfo(ITfDisplayAttributeInfo** result) {
    if (!result) return E_POINTER;
        *result = new (std::nothrow) Attribute(); return *result ? S_OK : E_OUTOFMEMORY;
}
HRESULT CreateAttributeEnumerator(IEnumTfDisplayAttributeInfo** result) {
    if (!result) return E_POINTER;
        *result = new (std::nothrow) Enumerator(); return *result ? S_OK : E_OUTOFMEMORY;
}
}
