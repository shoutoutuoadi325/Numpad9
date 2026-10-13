# MSIX input service backend

This backend targets Windows 11 24H2 (build 26100) or later through the real
`Windows.UI.Input.Preview.Text` WinRT APIs. It reuses `numpad9::Engine`; it does
not register the classic TSF DLL, write machine-wide registration, install a
keyboard hook, synthesize keys, use the clipboard, or fall back to processing
other applications through an accessibility automation channel.

Microsoft requires both an approved Text Input Method custom capability
(`Microsoft.appCategory.textInputMethod_8wekyb3d8bbwe`) for Store distribution
and a Limited Access Features unlock token. Package signing by the Store does
not grant either permission. Without actual configuration, the backend reports
`NotConfigured` and never creates a system keyboard processor.

## Host contract

The host initializes WinRT on its STA UI thread and keeps its message loop
running. Construct, start, select, toggle and stop `InputService` on that thread.
The callback is synchronous on the same thread; it must not destroy or restart
the service from inside a callback. The host reads the snapshot and renders
the candidate list in a window that does not activate or steal input focus.

`Config` holds four strings: Microsoft's supplied feature ID, granted token,
permission attestation and approved input profile identifier. Do not invent
an identifier or use the existing native TSF GUID string as an assumed WinRT
input profile. Do not log the token. `Start` returning true means the processor
was created and events attached; `Snapshot.active` determines whether Windows
has actually activated it. It can remain `WaitingForActivation`.

Candidate invocations supply both their index and `Snapshot.revision` to
`SelectCandidate`, so clicks from an old context cannot commit into a new one.
Keyboard support includes the phone positional layout with Num Lock both on
and off, ordinary keys passing through, English/Pinyin switching, candidate
selection and paging, Backspace, cancellation, punctuation and confirmation.

Edits use the key event's own `TextEditSession`, system compositions and explicit
`SubmitPayload`. The engine transaction advances only when the system accepts
its edit payload. The backend checks private/password/PIN scopes and disabled
read/write features before reading selection text or composing. It refuses to
adopt another processor's composition or overwrite a composition whose text
no longer matches its own delivered digits. It retains only its current digits,
candidates and the selected text necessary to restore that selection on cancel.
Focus loss and external composition termination erase those buffers; they do
not attempt to edit an unrelated newly focused control.

## Remaining external approval and verification

Public Microsoft documentation exposes `CreateKeyboardInputProcessor(string
inputProfile)` but does not describe a general public profile registration
manifest extension or that parameter's onboarding/lifecycle contract. Microsoft
must confirm the granted input profile and how Windows activates it across
target applications, updates and uninstall. A capability declaration or a
compiled backend alone does not prove that lifecycle.

The SDK header must exist to compile the actual backend. An older SDK compiles
a fail-closed stub reporting `UnsupportedWindows`; release CI must verify the
header and build the real branch. End-to-end input payload/keyboard behavior
can be exercised only after the package identity, unlock configuration and
profile onboarding are granted. The implementation currently remains gated
pending those approvals and genuine system input execution.

Primary references:

- [Input-method API namespace and approvals](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text?view=winrt-26100)
- [TextInputService methods and build requirements](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.textinputservice?view=winrt-26100)
- [CreateKeyboardInputProcessor](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.textinputservice.createkeyboardinputprocessor?view=winrt-26100)
- [LimitedAccessFeatures.TryUnlockFeature](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.limitedaccessfeatures.tryunlockfeature?view=winrt-26100)
- [TextEditSession](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.texteditsession?view=winrt-26100)
- [Microsoft's current SDK projection](https://github.com/microsoft/windows-rs/blob/master/crates/libs/windows/src/Windows/UI/Input/Preview/Text/mod.rs)
