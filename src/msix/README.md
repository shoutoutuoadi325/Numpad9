# MSIX input service backend

`Numpad9Store.exe` uses the real `Windows.UI.Input.Preview.Text` WinRT APIs on
Windows 11 24H2 (build 26100) or later. It shares `numpad9::Engine` with the
traditional TSF backend. Package identity, authorization and packaging are
documented in [the MSIX guide](../../docs/store/msix.md).

## Configuration and lifecycle

`Config.featureId`, `token` and `attestation` come from Microsoft's LAF grant for
the package family. `inputProfile` is an optional Windows input-profile binding;
when empty, `Start` reads the active profile through
`ITfInputProcessorProfileMgr::GetActiveProfile`. It is separate from the three
grant fields. The app does not register a new Win+Space entry or change the
user's default keyboard. Never log the token or include it in source archives.

Construct, start, select, toggle and stop `InputService` on the host's initialized
WinRT STA thread, with its message loop running. Agile Windows event callbacks
can arrive on another thread. The backend serializes state with a recursive
mutex; the host's `Changed` handler posts a window message, then reads
`GetSnapshot()` and renders on the UI thread. A callback must not destroy or
restart the service. Clear the host's notification-window reference before
stopping and destroying the UI.

`Start` returning true means the processor and event subscriptions were created.
`Snapshot.active` and `hasFocus` describe current Windows activation and eligible
text focus. Candidate actions carry `Snapshot.revision` so an old popup cannot
commit into a new context. If `anchorValid` is false, the host uses a monitor
work-area fallback for candidate placement.

## Input ownership

Key handling uses scan codes and extended-key flags to distinguish the keypad
from ordinary navigation keys. Accepted keydowns and keyups are paired. Input
mode belongs to Numpad9 rather than changing the bound keyboard's settings.

Edits use the key event's `TextEditSession`, owned system compositions and
`SubmitPayload`. Engine state advances only after Windows accepts the payload.
The backend checks protected scopes and disabled read/write features before
reading selection text or composing, and does not adopt another processor's
composition. It retains only current digits, candidates and the selection needed
for cancellation. Focus loss or external termination clears those buffers.

API references: [TextInputService](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.textinputservice?view=winrt-26100),
[KeyboardInputProcessor](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.keyboardinputprocessor?view=winrt-26100),
[TextEditSession](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.texteditsession?view=winrt-26100).
