// Windows-only COM lifecycle smoke test. No registry writes or SendInput.
// A standalone TSF host client ID is not an activated text-service client ID;
// TSF owns successful service activation after profile registration.
#include <windows.h>
#include <msctf.h>
#include <iostream>
#include "identity.h"

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Expected absolute IME DLL path\n"; return 1; }
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
    CoUninitialize();
    if (!result) std::cout << "TSF COM factory, invalid-state handling and unload checks passed.\n";
    return result;
}
