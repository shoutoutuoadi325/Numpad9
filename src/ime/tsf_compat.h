#pragma once
#include <windows.h>
#include <msctf.h>
#include <ctfutb.h>

#ifndef __ITfLangBarItemButton_INTERFACE_DEFINED__
#define __ITfLangBarItemButton_INTERFACE_DEFINED__
enum TfLBIClick { TF_LBI_CLK_RIGHT = 1, TF_LBI_CLK_LEFT = 2 };
struct ITfMenu;
struct ITfLangBarItemButton : ITfLangBarItem {
    virtual HRESULT STDMETHODCALLTYPE OnClick(TfLBIClick, POINT, const RECT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE InitMenu(ITfMenu*) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIcon(HICON*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetText(BSTR*) = 0;
};
#endif

#ifndef TF_CLIENTID_NULL
#define TF_CLIENTID_NULL ((TfClientId)0)
#endif
#ifndef TF_INVALID_GUIDATOM
#define TF_INVALID_GUIDATOM ((TfGuidAtom)0)
#endif
#ifndef __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
#define __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
struct ITfDisplayAttributeProvider : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo(REFGUID, ITfDisplayAttributeInfo**) = 0;
};
#endif
#ifndef __ITfTextLayoutSink_INTERFACE_DEFINED__
#define __ITfTextLayoutSink_INTERFACE_DEFINED__
enum TfLayoutCode { TF_LC_CREATE = 0, TF_LC_CHANGE = 1, TF_LC_DESTROY = 2 };
struct ITfTextLayoutSink : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE OnLayoutChange(ITfContext*, TfLayoutCode, ITfContextView*) = 0;
};
#endif

// MinGW's msctf.h currently omits these modern TSF interfaces. These declarations
// preserve the Windows SDK COM vtable order; the real SDK declarations take priority.
#ifndef __ITfTextInputProcessorEx_INTERFACE_DEFINED__
#define __ITfTextInputProcessorEx_INTERFACE_DEFINED__
struct ITfTextInputProcessorEx : ITfTextInputProcessor {
    virtual HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr*, TfClientId, DWORD) = 0;
};
#endif
#ifndef __ITfUIElement_INTERFACE_DEFINED__
#define __ITfUIElement_INTERFACE_DEFINED__
struct ITfUIElement : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetDescription(BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGUID(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Show(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsShown(BOOL*) = 0;
};
#endif
#ifndef __ITfCandidateListUIElement_INTERFACE_DEFINED__
#define __ITfCandidateListUIElement_INTERFACE_DEFINED__
struct ITfCandidateListUIElement : ITfUIElement {
    virtual HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSelection(UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetString(UINT, BSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPageIndex(UINT*, UINT, UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPageIndex(UINT*, UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT*) = 0;
};
#endif
#ifndef __ITfCandidateListUIElementBehavior_INTERFACE_DEFINED__
#define __ITfCandidateListUIElementBehavior_INTERFACE_DEFINED__
struct ITfCandidateListUIElementBehavior : ITfCandidateListUIElement {
    virtual HRESULT STDMETHODCALLTYPE SetSelection(UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE Finalize() = 0;
    virtual HRESULT STDMETHODCALLTYPE Abort() = 0;
};
#endif
#ifndef __ITfUIElementMgr_INTERFACE_DEFINED__
#define __ITfUIElementMgr_INTERFACE_DEFINED__
struct IEnumTfUIElements;
struct ITfUIElementMgr : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE BeginUIElement(ITfUIElement*, BOOL*, DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateUIElement(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE EndUIElement(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUIElement(DWORD, ITfUIElement**) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumUIElements(IEnumTfUIElements**) = 0;
};
#endif

namespace numpad9::ime {
inline constexpr GUID kIidLangBarButton = {0x28c7f1d0, 0xde25, 0x11d2, {0xaf, 0xdd, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}};
inline constexpr GUID kInputModeItem = {0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};
inline constexpr GUID kSystrayCategory = {0x25504fb4, 0x7bab, 0x4bc1, {0x9c, 0x69, 0xcf, 0x81, 0x89, 0x0f, 0x0e, 0xf5}};
inline constexpr GUID kIidTextLayoutSink = {0x2af2d06a, 0xdd5b, 0x4927, {0xa0, 0xb4, 0x54, 0xf1, 0x9c, 0x91, 0xfa, 0xde}};
// These two symbols are missing from MinGW's uuid import library.
inline constexpr GUID kInputScopeProperty = {0x1713dd5a, 0x68e7, 0x4a5b, {0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d}};
inline constexpr GUID kIidInputScope = {0xfde1eaee, 0x6924, 0x4cdf, {0x91, 0xe7, 0xda, 0x38, 0xcf, 0xf5, 0x55, 0x9d}};
inline constexpr GUID kIidDisplayAttributeProvider = {0xfee47777, 0x163c, 0x4769, {0x99, 0x6a, 0x6e, 0x9c, 0x50, 0xad, 0x8f, 0x54}};
inline constexpr GUID kIidTipEx = {0x6e4e2102, 0xf9cd, 0x433d, {0xb4, 0x96, 0x30, 0x3c, 0xe0, 0x3a, 0x65, 0x07}};
inline constexpr GUID kIidUiElement = {0xea1ea137, 0x19df, 0x11d7, {0xa6, 0xd2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
inline constexpr GUID kIidCandidateList = {0xea1ea138, 0x19df, 0x11d7, {0xa6, 0xd2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
inline constexpr GUID kIidCandidateBehavior = {0x85fad185, 0x58ce, 0x497a, {0x94, 0x60, 0x35, 0x53, 0x66, 0xb6, 0x4b, 0x9a}};
inline constexpr GUID kIidUiElementMgr = {0xea1ea135, 0x19df, 0x11d7, {0xa6, 0xd2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
inline constexpr GUID kUiElementCategory = {0x49d2f9cf, 0x1f5e, 0x11d7, {0xa6, 0xd3, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
inline constexpr GUID kImmersiveCategory = {0x13a016df, 0x560b, 0x46cd, {0x94, 0x7a, 0x4c, 0x3a, 0xf1, 0xe0, 0xe3, 0x5d}};
}
