#include "tsf_compat.h"
#include "identity.h"
#include "com.h"
#include <algorithm>
#include <string>
#include <vector>

namespace numpad9::ime {
namespace {
const std::wstring kComKey = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + kClsidString;
const std::wstring kTipKey = std::wstring(L"SOFTWARE\\Microsoft\\CTF\\TIP\\") + kClsidString;
const GUID kCategories[] = {GUID_TFCAT_TIP_KEYBOARD, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
                           kUiElementCategory, kImmersiveCategory, kSystrayCategory};
class ComInitialization {
    HRESULT status_;
public:
    ComInitialization() : status_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComInitialization() { if (SUCCEEDED(status_)) CoUninitialize(); }
    HRESULT status() const { return status_ == RPC_E_CHANGED_MODE ? S_OK : status_; }
};
class RegistryKey {
    HKEY key_ = nullptr;
public:
    ~RegistryKey() { if (key_) RegCloseKey(key_); }
    HKEY get() const { return key_; }
    HKEY* put() { return &key_; }
};
struct RegistryValue { std::wstring name; DWORD type; std::vector<BYTE> data; };
struct RegistrySnapshot {
    bool exists = false;
    std::vector<RegistryValue> values;
    std::vector<std::pair<std::wstring, RegistrySnapshot>> children;
};
HRESULT Snapshot(HKEY root, const std::wstring& path, RegistrySnapshot& result, unsigned depth = 0) {
    if (depth > 32) return E_UNEXPECTED;
    RegistryKey key;
    auto error = RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, key.put());
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return S_OK;
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    result.exists = true;
    DWORD maxName = 0, maxData = 0, maxSubkey = 0;
    error = RegQueryInfoKeyW(key.get(), nullptr, nullptr, nullptr, nullptr, &maxSubkey, nullptr, nullptr, &maxName, &maxData, nullptr, nullptr);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    if (maxData > 1024 * 1024) return E_OUTOFMEMORY;
    for (DWORD i = 0; ; ++i) {
        RegistryValue value;
        std::vector<wchar_t> name(maxName + 2);
        DWORD nameLength = static_cast<DWORD>(name.size()), dataLength = maxData;
        value.data.resize(maxData);
        error = RegEnumValueW(key.get(), i, name.data(), &nameLength, nullptr, &value.type, value.data.data(), &dataLength);
        if (error == ERROR_NO_MORE_ITEMS) break;
        if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
        value.name.assign(name.data(), nameLength); value.data.resize(dataLength);
        result.values.push_back(std::move(value));
    }
    for (DWORD i = 0; ; ++i) {
        std::vector<wchar_t> name(maxSubkey + 2); DWORD nameLength = static_cast<DWORD>(name.size());
        error = RegEnumKeyExW(key.get(), i, name.data(), &nameLength, nullptr, nullptr, nullptr, nullptr);
        if (error == ERROR_NO_MORE_ITEMS) break;
        if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
        std::wstring childName(name.data(), nameLength); RegistrySnapshot child;
        auto hr = Snapshot(key.get(), childName, child, depth + 1); if (FAILED(hr)) return hr;
        result.children.emplace_back(childName, std::move(child));
    }
    return S_OK;
}
HRESULT WriteSnapshot(HKEY root, const std::wstring& path, const RegistrySnapshot& snapshot) {
    if (!snapshot.exists) return S_OK;
    RegistryKey key;
    auto error = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, key.put(), nullptr);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    for (const auto& value : snapshot.values) {
        error = RegSetValueExW(key.get(), value.name.c_str(), 0, value.type, value.data.data(), static_cast<DWORD>(value.data.size()));
        if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    }
    for (const auto& child : snapshot.children) {
        auto hr = WriteSnapshot(key.get(), child.first, child.second); if (FAILED(hr)) return hr;
    }
    return S_OK;
}
HRESULT DeleteTree(HKEY root, const std::wstring& path) {
    const auto error = RegDeleteTreeW(root, path.c_str());
    return error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(error);
}
HRESULT Restore(HKEY root, const std::wstring& path, const RegistrySnapshot& snapshot) {
    auto hr = DeleteTree(root, path); return FAILED(hr) ? hr : WriteSnapshot(root, path, snapshot);
}
std::wstring Path() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(gModule, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) return {};
        if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
        if (buffer.size() >= 32768) return {};
        buffer.resize(buffer.size() * 2);
    }
}
HRESULT WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
    return HRESULT_FROM_WIN32(RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))));
}
HRESULT RegisterCom(const std::wstring& module) {
    RegistryKey key, server;
    auto error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kComKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, key.put(), nullptr);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    auto hr = WriteString(key.get(), nullptr, kName); if (FAILED(hr)) return hr;
    error = RegCreateKeyExW(key.get(), L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, server.put(), nullptr);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    hr = WriteString(server.get(), nullptr, module); if (FAILED(hr)) return hr;
    return WriteString(server.get(), L"ThreadingModel", L"Apartment");
}
HRESULT RegisteredPath(std::wstring& result) {
    RegistryKey key; const auto path = kComKey + L"\\InprocServer32";
    auto error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, key.put());
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    DWORD size = 0, type = 0;
    error = RegQueryValueExW(key.get(), nullptr, nullptr, &type, nullptr, &size);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    if (type != REG_SZ || size > 65536 || size % sizeof(wchar_t)) return E_UNEXPECTED;
    std::vector<wchar_t> value(size / sizeof(wchar_t) + 1, 0);
    error = RegQueryValueExW(key.get(), nullptr, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &size);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    result = value.data(); return S_OK;
}
HRESULT Register() {
    ComInitialization initialization;
    if (FAILED(initialization.status())) return initialization.status();
    const auto module = Path(); if (module.empty()) return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
    const auto separator = module.find_last_of(L"\\/");
    const auto dictionary = module.substr(0, separator + 1) + L"lexicon.tsv";
    const DWORD attributes = GetFileAttributesW(dictionary.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    RegistrySnapshot oldCom, oldTip;
    auto hr = Snapshot(HKEY_LOCAL_MACHINE, kComKey, oldCom); if (FAILED(hr)) return hr;
    hr = Snapshot(HKEY_LOCAL_MACHINE, kTipKey, oldTip); if (FAILED(hr)) return hr;
    ComPtr<ITfInputProcessorProfileMgr> profiles; ComPtr<ITfCategoryMgr> categories;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(profiles.put()));
    if (FAILED(hr)) return hr;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(categories.put()));
    if (FAILED(hr)) return hr;
    std::vector<GUID> previouslyRegistered;
    ComPtr<IEnumGUID> enumerator;
    hr = categories->EnumCategoriesInItem(kClsid, enumerator.put());
    if (FAILED(hr)) return hr;
    if (enumerator) { GUID category{}; ULONG fetched = 0; while (enumerator->Next(1, &category, &fetched) == S_OK && fetched) previouslyRegistered.push_back(category); }
    bool profileRegistered = false;
    std::vector<GUID> addedCategories;
    addedCategories.reserve(std::size(kCategories));
    try {
      hr = RegisterCom(module);
      if (SUCCEEDED(hr)) {
        // The profile is listed in language settings but never becomes the default
        // input method and is never activated during installation.
        hr = profiles->RegisterProfile(kClsid, kLanguage, kProfile, kName,
            static_cast<ULONG>(std::size(kName) - 1), module.c_str(), static_cast<ULONG>(module.size()),
            0, nullptr, 0, FALSE, 0);
        profileRegistered = SUCCEEDED(hr);
      }
      if (SUCCEEDED(hr)) for (const auto& category : kCategories) {
        hr = categories->RegisterCategory(kClsid, category, kClsid);
        if (FAILED(hr)) break;
        const auto old = std::find_if(previouslyRegistered.begin(), previouslyRegistered.end(), [&](const GUID& guid) { return guid == category; });
        if (old == previouslyRegistered.end()) addedCategories.push_back(category);
      }
    } catch (const std::bad_alloc&) { hr = E_OUTOFMEMORY; }
      catch (...) { hr = E_FAIL; }
    if (FAILED(hr)) {
        for (const auto& category : addedCategories) categories->UnregisterCategory(kClsid, category, kClsid);
        if (profileRegistered && !oldTip.exists) profiles->UnregisterProfile(kClsid, kLanguage, kProfile, 0);
        // Restore only this TIP's own keys. Never restore broad system trees or
        // another provider's registry state. The installer also re-registers the
        // previous version if any registration stage fails.
        const auto comRestore = Restore(HKEY_LOCAL_MACHINE, kComKey, oldCom);
        const auto tipRestore = Restore(HKEY_LOCAL_MACHINE, kTipKey, oldTip);
        if (FAILED(comRestore)) return comRestore;
        if (FAILED(tipRestore)) return tipRestore;
    }
    return hr;
}
HRESULT Unregister() {
    ComInitialization initialization;
    if (FAILED(initialization.status())) return initialization.status();
    std::wstring registered; auto hr = RegisteredPath(registered);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) return S_OK;
    if (FAILED(hr)) return hr;
    const auto module = Path();
    if (CompareStringOrdinal(registered.c_str(), -1, module.c_str(), -1, TRUE) != CSTR_EQUAL) return S_FALSE;
    ComPtr<ITfInputProcessorProfileMgr> profiles; ComPtr<ITfCategoryMgr> categories;
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(profiles.put()));
    if (FAILED(hr)) return hr;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(categories.put()));
    if (FAILED(hr)) return hr;
    HRESULT firstError = S_OK;
    for (const auto& category : kCategories) {
        hr = categories->UnregisterCategory(kClsid, category, kClsid);
        if (FAILED(hr) && SUCCEEDED(firstError)) firstError = hr;
    }
    hr = profiles->UnregisterProfile(kClsid, kLanguage, kProfile, 0);
    if (FAILED(hr) && SUCCEEDED(firstError)) firstError = hr;
    if (FAILED(firstError)) return firstError;
    // Use the TSF registration API above for profile removal. The standard COM
    // in-proc server entry is owned by this DLL and removed in its registry view.
    return DeleteTree(HKEY_LOCAL_MACHINE, kComKey);
}
}
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    try { return numpad9::ime::Register(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    try { return numpad9::ime::Unregister(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
