// Windows-only COM/TSF lifecycle smoke test. No registry writes or SendInput.
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
    ITfThreadMgr* manager = nullptr;
    TfClientId client = 0;
    auto check = [&result](HRESULT hr, const char* operation) {
        if (FAILED(hr)) { std::cerr << operation << ": 0x" << std::hex << hr << '\n'; result = 4; }
        return SUCCEEDED(hr);
    };
    if (!getClass || !canUnload || !GetProcAddress(dll, "DllRegisterServer") ||
        !GetProcAddress(dll, "DllUnregisterServer")) { result = 5; }
    if (!result && check(getClass(numpad9::ime::kClsid, IID_IClassFactory,
        reinterpret_cast<void**>(&factory)), "Get class factory") &&
        check(factory->CreateInstance(nullptr, IID_ITfTextInputProcessor,
        reinterpret_cast<void**>(&tip)), "Create TIP") &&
        check(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfThreadMgr, reinterpret_cast<void**>(&manager)), "Create TSF thread manager") &&
        check(manager->Activate(&client), "Activate TSF thread manager")) {
        if (check(tip->Activate(manager, client), "Activate TIP")) {
            check(tip->Deactivate(), "Deactivate TIP");
        }
    }
    if (tip) tip->Release();
    if (factory) factory->Release();
    if (manager) {
        if (client != 0) manager->Deactivate();
        manager->Release();
    }
    if (!result && canUnload() != S_OK) { std::cerr << "COM references leaked\n"; result = 6; }
    // Do not unload a module whose COM objects remain live.
    if (!canUnload || canUnload() == S_OK) FreeLibrary(dll);
    CoUninitialize();
    if (!result) std::cout << "TSF COM activation/deactivation smoke passed.\n";
    return result;
}
