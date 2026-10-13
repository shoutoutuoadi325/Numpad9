#pragma once
#include "tsf_compat.h"
#include "com.h"
#include "../core/engine.h"
#include <functional>

namespace numpad9::ime {
class ModeButton final : public RefCount, public ITfLangBarItemButton, public ITfSource {
    std::function<HRESULT()> toggle_;
    ComPtr<ITfLangBarItemSink> sink_;
    Mode mode_ = Mode::Pinyin;
    bool shown_ = true;
public:
    explicit ModeButton(std::function<HRESULT()> toggle) : toggle_(std::move(toggle)) {}
    void Detach() { toggle_ = {}; sink_.reset(); }
    void SetMode(Mode mode);
    STDMETHODIMP QueryInterface(REFIID, void**) override;
    STDMETHODIMP_(ULONG) AddRef() override { return Retain(); }
    STDMETHODIMP_(ULONG) Release() override { return Drop(); }
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO*) override;
    STDMETHODIMP GetStatus(DWORD*) override;
    STDMETHODIMP Show(BOOL) override;
    STDMETHODIMP GetTooltipString(BSTR*) override;
    STDMETHODIMP OnClick(TfLBIClick, POINT, const RECT*) override;
    STDMETHODIMP InitMenu(ITfMenu*) override { return E_NOTIMPL; }
    STDMETHODIMP OnMenuSelect(UINT) override { return E_NOTIMPL; }
    STDMETHODIMP GetIcon(HICON*) override;
    STDMETHODIMP GetText(BSTR*) override;
    STDMETHODIMP AdviseSink(REFIID, IUnknown*, DWORD*) override;
    STDMETHODIMP UnadviseSink(DWORD) override;
};
}
