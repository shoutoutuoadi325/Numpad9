# Native TSF input service

`Numpad9Ime.dll` is a C++17, apartment-threaded, in-process Windows Text Services
Framework service. It runs when the user selects its Simplified Chinese profile;
English is a mode of the same profile. User controls and keypad positions are in
[the main README](../../README.md).

## Installation and registration

Stable CLSID and profile identities are defined in `identity.h` and shared by the
x64 and x86 DLLs. Registration uses `ITfInputProcessorProfileMgr::RegisterProfile`
and `ITfCategoryMgr`, plus standard COM values under
`HKLM\SOFTWARE\Classes\CLSID` with `ThreadingModel=Apartment`. Both architectures
are installed on x64 Windows to support text applications of either bitness.

Each DLL reads the adjacent `lexicon.tsv` with inherited Program Files read
permissions. Registration requires that dictionary, rolls back its own COM/TIP
keys and new categories on failure, and reports errors. The installer restores
the preceding version if multi-architecture registration fails. Unregistration
checks that the current COM path still belongs to this DLL before removing it,
so uninstalling an old version cannot unregister a newer one. No default input
method, startup entry, service or driver is installed.

## Composition ownership

Document changes occur inside synchronous TSF read/write edit sessions using
`ITfRange`, `ITfComposition` or `ITfInsertAtSelection`. A key is consumed only when
its edit session accepts the operation; denied locks leave the key to the app.
There is no global hook, simulated input, clipboard replacement or delayed
injection into another application's focus.

The service checks selection input scopes and keyboard-disabled, empty-context
and keyboard-open compartments before handling keys and again in the write
session. Password, private and PIN scopes, read-only contexts and secure-mode
activation are excluded. Failed scope or selection reads fail closed.

A composition owns only its range. Original selected text is retained in memory
for cancellation; selections above 65,536 UTF-16 units are left untouched.
Normal focus changes confirm the candidate while preserving the app selection.
If cleanup is denied, an asynchronous session ends only the old composition;
forced termination preserves visible text and relinquishes the range. Epoch
checks invalidate queued work after context changes, and sink references are
released during cleanup. Mode and composition live in the service instance;
there is no history, personal dictionary, persistence or network access.

## Candidate and mode UI

`ITfCandidateListUIElementBehavior` exposes candidates, selection, paging,
finalization and cancellation through the TSF UI element manager. A host that
consumes that element or requests UI-only operation suppresses the fallback
popup. The popup is owned and nonactivating, follows the caret and monitor work
area, and respects DPI, system colors and touch-keyboard occlusion.

The UI Automation provider uses `IME_Candidate_Window`, selection/invoke
patterns and generation checks. Invocations are posted to the IME apartment;
retained providers keep the DLL loaded and become unavailable when their window
or candidate generation expires. The final popup destruction unregisters its
DLL-owned window class.

The language-bar button uses `GUID_LBI_INPUTMODE`, shows `中` or `A`, and confirms
composition before changing modes. Its callback and update sink detach on
deactivation. Registered categories cover keyboard TIP, display attributes,
UI elements, immersive support and system-tray support.

`tsf_compat.h` supplies limited ABI declarations missing from MinGW; Windows SDK
definitions take precedence. The DLL exports the standard COM class-factory,
unload, register and unregister entry points. Profile icons are embedded.

References: [TSF registration](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration),
[compositions](https://learn.microsoft.com/en-us/windows/win32/tsf/compositions),
[capability categories](https://learn.microsoft.com/en-us/windows/win32/tsf/predefined-category-values).
