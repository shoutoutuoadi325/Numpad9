# Native TSF input service

`Numpad9Ime.dll` is a C++17, apartment-threaded, in-process Text Services
Framework text service. It uses neither global keyboard hooks, `SendInput`, nor
the clipboard. It runs only when a user selects its input profile. Installation
registers a selectable Simplified Chinese profile; it does not change the default
input method. English is a mode of the same profile.

Stable identities are defined in `identity.h`. Both x64 and x86 builds use the
same CLSID/profile. COM activation, language profiles and categories are registered
in the architecture of the `regsvr32` process. An x64 Windows installation must
install/register both architectures to support 32-bit applications. The installer
must place `lexicon.tsv` beside each DLL in its respective Program Files directory,
with the directory's standard inherited read permissions. The dictionary is opened
read-only once per service instance. There is no personal dictionary, learning,
input history, telemetry, network access or persistence of composing text.

## Input behavior

The physical positions of the keypad are interpreted as the phone layout:

```text
physical legends       logical phone positions
   7 8 9                    1 2 3
   4 5 6                    4 5 6
   1 2 3                    7 8 9
```

Mapping handles both NumLock states and rejects extended dedicated navigation
keys as numeric keypad keys. The normal number row remains a normal number row.
Logical keys 2–9 form the offline Pinyin/English lookup sequence. Logical 1 inserts
`，` in Pinyin mode and `,` in English mode; keypad decimal inserts `。` or `.`.
Punctuation confirms an existing candidate before inserting the mark.

- `Ctrl+Shift+E` or keypad `/`: confirm preedit, then switch Pinyin/English.
- Keypad `0`: confirm preedit, or insert a space while idle.
- Space/Enter: confirm the currently selected candidate; if there are no
  candidates, keep the visible digits.
- Backspace: remove one logical digit; deleting the final digit cancels preedit.
- Escape: cancel preedit and restore the original selected text, if typing replaced
  a selection. Restoration applies only while this service owns the composition.
- Dedicated Up/Left: select previous; Down/Right or keypad `*`: select next.
- Keypad `+` / `−`, or dedicated PageDown/PageUp: next/previous candidate page.
- Clicking a candidate, or its UI Automation Invoke pattern: select and confirm.
- Other typed keys and Tab: finish the candidate and pass the key to the app.

The mode lasts for the service instance in the current application's input thread;
it starts in Pinyin for a new instance. It is not shared through hidden files or
registry writes. There is no arbitrary phrase generation or personalized ranking.
Unmatched input has no candidates and can be committed as digits.

## Composition ownership and protected contexts

All document changes occur inside TSF read/write edit sessions and use
`ITfRange`/`ITfComposition` or `ITfInsertAtSelection`. A key is eaten only after
its synchronous edit session accepts it. If the app denies a synchronous lock,
the original key remains available to the application. There is no delayed
injection into a different application's focus.

The service checks the focused context's selection input scopes and the
keyboard-disabled, empty-context and keyboard-open compartments before handling
keys, and checks them again inside the write session. Password, numeric password,
private, numeric PIN, alphanumeric PIN and PIN-set scopes are excluded. Read-only
contexts and secure-mode activation are excluded. Failed scope/selection reads
fail closed. Correct exclusion depends on the host application identifying its
protected input scope or disabling keyboard composition correctly.

A composition modifies only its own range. The original selected text is retained
in process memory during composition so cancellation can restore it; selected text
above 65,536 UTF-16 units is left untouched by this service. Normal focus changes
confirm the candidate while preserving the app's current selection. If a text
store refuses cleanup during deactivation, an asynchronous session only ends the
specific old composition; it does not rewrite document text. In that fallback,
visible raw preedit may remain. If the host forcibly terminates the composition,
its visible text is preserved and the service relinquishes the range. Epoch checks
make obsolete queued focus/layout edits harmless. Context and composition sink
references are disconnected on cleanup/deactivation to avoid ownership cycles.

## Candidate presentation and accessibility

The service exposes `ITfCandidateListUIElementBehavior` through the TSF UI element
manager. It implements descriptions, document ownership, candidate count and
strings, selection, page indices, visibility, selection changes, finalization and
abort. Begin/Update/EndUIElement are paired. If the host consumes the element or
activates in UI-only mode, the service does not display its fallback popup.

For desktop presentation the popup is an owned, nonactivating window near the TSF
composition caret. It respects the host's DPI awareness, monitor work area,
framework touch-keyboard occlusion, system colors and high-contrast settings.
Text layout changes update placement. The popup emits the IME show/hide/change
accessibility events and closes on focus loss. The last popup destruction
unregisters its DLL-owned window class before COM permits unloading.

Its UI Automation provider presents a list with AutomationId
`IME_Candidate_Window`, candidate names and spelling help, selection and invoke
patterns, fragment navigation and bounding rectangles. It emits menu opened,
menu closed and selected-item events. UIA candidate actions are posted to the
IME's apartment thread and use a candidate-generation check, so an obsolete
accessibility object cannot select a new, unrelated candidate. Providers retained
by an accessibility client keep the COM DLL loaded safely and become unavailable
when their window or candidate generation is gone.

The native language-bar/input-indicator mode button shows `中` or `A` while idle.
It exposes the standard `GUID_LBI_INPUTMODE`, a mode tooltip, a pushbutton toggle,
and an updated monochrome mode icon. Clicking it switches mode and confirms a
composition first. The button's weak service callback and item-update sink are
detached during deactivation, so retained shell objects cannot call a destroyed
service. Icons are generated in the DLL and sized for the current host DPI.

Registered capabilities: keyboard TIP, display attribute provider, UI elements,
immersive support and system-tray support. Secure mode, COM-less capability,
custom touch layout and search integration are not advertised. The DLL exports
standard COM class-factory, unload, register and unregister entry points. Profile
icons are loaded from the embedded resource, not an external ICO file.

## Registration and release validation

Registration uses `ITfInputProcessorProfileMgr::RegisterProfile` and
`ITfCategoryMgr`. Standard COM server values are written under
`HKLM\SOFTWARE\Classes\CLSID` with `ThreadingModel=Apartment`. Registration checks
that the adjacent dictionary exists, reports actual failures and rolls back its
own COM/TIP keys and newly added categories on failure. The installer additionally
re-registers the previous version if a multi-architecture installation fails.
Unregistration checks that its architecture's current COM server path still
belongs to this DLL, so removing an old version cannot unregister a newer one.

Both x64 and x86 cross builds have been compiled and the four undecorated COM
export names inspected. Compilation does not establish interactive Windows
compatibility. Before publication, run the repository's Windows tests and manual
matrix on Windows 10/11, including NumLock states, Word, Notepad, browser fields,
32-bit apps, password/PIN fields, UAC, monitor/DPI changes, Narrator, a UIless host,
selection cancellation, mid-composition focus change, update and uninstall. Sign
the native DLLs and installer with the publisher's trusted Authenticode certificate.
Microsoft Store certification remains an external process and must not be claimed
as already passed by a local build.

Implementation references:

- [IME requirements](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements)
- [TSF registration](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)
- [TSF compositions](https://learn.microsoft.com/en-us/windows/win32/tsf/compositions)
- [Capability categories](https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-category-values)

`tsf_compat.h` contains limited ABI declarations for interfaces omitted by MinGW's
headers; the full Windows SDK definitions take precedence. The COM vtable order
and GUIDs follow the Windows SDK. UIA numeric identifiers in the MinGW-only branch
work around its current incompatible core/client header revisions.
