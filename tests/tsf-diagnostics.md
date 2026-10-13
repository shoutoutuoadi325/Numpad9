# Registered TSF activation diagnostics

These checks diagnose `ime_smoke --registered` without relaxing its success conditions or constructing a fake input context. No input text or keystroke values need to be logged.

## Facts from the current source

- `active=0x0` prints the HRESULT of `GetActiveProfile`, not the selected profile identity. Print the selected `dwProfileType`, `langid`, `clsid`, `guidProfile` and flags separately.
- `GetForeground == S_FALSE` means there is no foreground text service. It is not success for this test. [GetForeground](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-getforeground).
- The current chained boolean short-circuits when the foreground service check fails. Thus `GetFocus` and `GetTop` may never run, and the combined error cannot establish that the RichEdit context is missing.
- The current `EM_SETEDITSTYLE` result and the actual Win32 focus/foreground state are discarded. Verify them before attributing a failure to the service.
- `ActivateEx` currently removes its key sink through `Deactivate` if a later mandatory stage fails. A successful key-sink registration followed by a source-sink failure can therefore produce the same final symptom as never loading the service.

## Host checks to add independently

Collect every result before combining the final assertion:

```cpp
TF_INPUTPROCESSORPROFILE selected{};
CLSID foreground = CLSID_NULL;
Com<ITfDocumentMgr> document;
Com<ITfContext> context;
const HRESULT profileHr = manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &selected);
const HRESULT foregroundHr = keys->GetForeground(&foreground);
const HRESULT documentHr = thread->GetFocus(&document.p);
const HRESULT contextHr = document.p ? document->GetTop(&context.p) : E_UNEXPECTED;
// Log each HRESULT, the non-null states, and actual GUIDs; then apply the
// unchanged required profile + foreground service + genuine context checks.
```

Use `StringFromGUID2` for GUID output. Report `GetFocus() == edit`, `GetForegroundWindow() == window` and `thread->IsThreadFocus(...)` separately. Do not equate the return value of `SetFocus` with success: it returns the previous focused window.

Capture the return from `EM_SETEDITSTYLE`, and inspect `EM_GETEDITSTYLE`. `SES_USECTF` must be present; `SES_NOIME` and `SES_CTFNOLOCK` should be absent. The latter pauses TSF input. [EM_SETEDITSTYLE](https://learn.microsoft.com/en-us/windows/win32/controls/em-seteditstyle), [EM_GETEDITSTYLE](https://learn.microsoft.com/en-us/windows/win32/controls/em-geteditstyle).

Query `ITfThreadMgrEx::GetActiveFlags` and report `TF_TMF_ACTIVATED`, `TF_TMF_NOACTIVATETIP`, `TF_TMF_SECUREMODE`, `TF_TMF_CONSOLE`, and `TF_TMF_UIELEMENTENABLEDONLY`. The test executable is a console program with a window; console mode is a hypothesis to check, not an established cause. [GetActiveFlags](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfthreadmgrex-getactiveflags).

After RichEdit has focus, dynamically obtain `TF_GetThreadMgr` from the system `msctf.dll` and call it on the same thread. Compare canonical `IID_IUnknown` pointers for the returned object and `thread`. Query both managers' real focus/context/foreground state if their identities differ. `TF_GetThreadMgr` can return `S_OK` with a null pointer, which must be reported distinctly. [TF_GetThreadMgr](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-tf_getthreadmgr).

Do not change `CoCreateInstance` to `TF_CreateThreadMgr` speculatively. Microsoft recommends `CoCreateInstance`; `TF_CreateThreadMgr` is not recommended. [TF_CreateThreadMgr](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-tf_createthreadmgr).

## Service activation stages

Instrument these stages only in a dedicated diagnostic build. Keep the four existing COM exports and normal release privacy behavior:

1. `DllGetClassObject` entry and requested interface result.
2. Class factory creation result.
3. `ActivateEx` entry, non-null thread, client identifier and activation flags.
4. `QueryInterface(IID_ITfKeystrokeMgr)` result.
5. `AdviseKeyEventSink(..., TRUE)` result.
6. `QueryInterface(IID_ITfSource)` result.
7. `AdviseSink(IID_ITfThreadMgrEventSink, ...)` result and whether its cookie is valid.
8. UI element manager interface result and whether it is mandatory for the activation flags.
9. `ActivateEx` success or exact failure before cleanup.
10. `Deactivate` entry and sink removal results.

An explicit compile definition prevents logging in ordinary builds; logging only operation names and HRESULTs avoids any input-content capture:

```cpp
static HRESULT TraceTsfStage(const wchar_t* stage, HRESULT hr) noexcept {
#if defined(NUMPAD9_TSF_DIAGNOSTICS)
    wchar_t message[256]{};
    swprintf_s(message, L"Numpad9 TSF %ls: HRESULT=0x%08lX\n",
               stage, static_cast<unsigned long>(hr));
    OutputDebugStringW(message);
#else
    (void)stage;
#endif
    return hr;
}
// Example at the owning source's existing call site:
// hr = TraceTsfStage(L"AdviseKeyEventSink", keys_->AdviseKeyEventSink(...));
```

Capture debug output with a debugger or a dedicated diagnostic collector while exercising the registered installed service. Absence of any factory/activation entry points to a registration, profile selection, module loading or host environment problem. An activation entry followed by a specific failed stage identifies the service-side operation to fix. If all mandatory stages succeed and the service is immediately deactivated, record the lifecycle order before changing input routing.

`AdviseKeyEventSink(TRUE)` is the documented way to install a foreground sink. Source-sink installation is separately fallible and its cookie becomes invalid on failure. [AdviseKeyEventSink](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-advisekeyeventsink), [AdviseSink](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfsource-advisesink).

Do not treat selected profile metadata alone as proof of live input processing, directly call `Activate` to manufacture a passing registered-profile result, or push a context detached from the test's actual text store. Keep the foreground sink and genuine focused edit context assertions.

Primary documentation checked on 2026-10-13.
