# Store and IME evidence matrix

Checked against Microsoft primary documentation on 2026-10-13. This internal matrix records implemented controls and validation ideas. Windows automated build/tests and release structural checks are used; manual physical QA suggestions are optional. It is not a Microsoft certification result.

| Requirement | Repository control | Automated verification / optional internal check |
| --- | --- | --- |
| EXE/MSI and offline payload | `packaging/Numpad9.iss`, static CRT in root CMake | Fresh VM without C++ redistributable, disconnect network, silent installation succeeds |
| Every shipped PE and installer trusted signed | `Sign-File.ps1`, Inno `SignedUninstaller`, `Validate-Release.ps1` | Public CA chain and timestamp for both DLLs, host, installer, actual generated installed uninstaller |
| Silent installation, UAC allowed | Documented Inno switches, no post-install launch | Standard user initiates install; only UAC; no wizard/error dialogs/reboot; record exit code |
| Immutable HTTPS package URL | URL/version check in validator | Download hosted EXE and compare SHA256 to signed tested artifact; prevent replacement; use new URL for each version |
| Accurate product/publisher/version | Inno metadata from explicit caller values | Programs list and EXE metadata agree with actual publisher and Partner Center |
| Clean uninstall | DLL unregister exports, version ownership check, file cleanup log, locked-file deferred deletion | Check HKLM 32/64 COM, TSF TIP/categories, shortcuts, files and uninstall record after restart; multi-user test |
| x86/x64 TSF support | Same CLSID and profile; separate DLLs, separate Program Files | Real x64 AND x86 editable applications work; no duplicate logical keyboard |
| App-container compatibility | TSF, dictionaries under Program Files | Store app text input, dictionary access, candidate UI and permissions; do not bypass sandbox |
| User chooses default keyboard | No default-layout registry writes; no forced activation | Windows Settings/Win+Space selection; existing default remains user's choice |
| User privacy | Offline engine; policy draft | Confirm no network or persistent input logging; test password scopes; complete real contact and publish policy |
| Reliability while apps hold DLLs | Version directories; registration restore on errors | Live composition during upgrade; forced registration failure rollback; uninstall while loaded then restart |
| No bundleware / services / drivers | Only host, input service, dictionary, license, policy | Exactly one uninstall entry, no driver/service/startup/task installed |
| Honest listing/assets | `listing.md` and automated screenshot artifact | Actual Windows screenshots; Store logo; matching claims, OS and architecture; age/market declarations |
| Open-source and dictionary obligations | Root GPL-3.0; `data/licenses` packaged | Publish full corresponding source for exact release, retain third-party attribution/license and change notice |
| Malware/security scan | Microsoft certification; optional internal scan | Scan exact signed installer and installed payload with current Defender; retain result and versions |
| Keyboard, contrast, DPI, screen reader | Native UI controls; QA checklist | Real testing at multiple DPI, high contrast, Narrator, keyboard-only operation; do not assert certified accessibility without results |

Primary references: [Package requirements](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-package-requirements), [certification](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-certification-process), [manual validation](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/manual-package-validation), [submission fields](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/create-app-submission), [IME requirements](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements), [TSF architecture](https://learn.microsoft.com/en-us/windows/win32/tsf/64-bit-platform-considerations), [Store policies](https://learn.microsoft.com/en-us/windows/apps/publish/store-policies).
