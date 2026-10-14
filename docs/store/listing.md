# Traditional EXE listing reference

The current Microsoft Store distribution uses the separate MSIX host described in [msix.md](msix.md). The text below documents the traditional TSF EXE; do not copy its OS requirements or registration instructions into the MSIX listing.

Product name: Numpad9. The reserved Partner Center name is authoritative. The following text describes the product's functions and supported environments.

## 简体中文

名称：Numpad9。

简短描述：把电脑小键盘变成手机布局的九键输入法，支持离线中文拼音和英文输入。

描述：

Numpad9 让你用 Windows 电脑的小键盘，以熟悉的手机九键布局输入中文拼音和英文。按键按照物理位置映射：电脑的 7、8、9 对应手机的 1、2、3，电脑的 1、2、3 对应手机的 7、8、9。无需按电脑键帽上的数字重新记忆布局。

它作为 Windows 输入法运行，提供本地候选和确认输入。中文使用内置离线拼音词典，英文提供九键候选。你可以在 Windows 输入法菜单中切换使用，在帮助窗口中查看按键示意和操作说明。内置词典覆盖范围有限；这一版本不提供云联想或个人词频学习。

适用于 Windows 10 1809 或更高版本及 Windows 11 的 x64 电脑。需要带数字小键盘的键盘；按物理位置识别按键，支持 Num Lock 开启和关闭。安装需要管理员授权；安装后在 Windows 的中文语言键盘选项中启用 Numpad9，再使用 Win+Space 切换。此版本不支持 ARM64、32 位 Windows 或 Windows S 模式。

Numpad9 的输入处理在本机完成，不上传打字内容，不包含广告或遥测。你可以通过 Windows 的应用设置卸载。使用 GPL-3.0 许可；对应源码和词典署名随版本提供。

功能条目：手机九键布局的小键盘映射；离线中文拼音候选；英文九键输入；Windows 输入法切换；键盘操作及帮助说明。

关键词草稿：九键、拼音、小键盘、输入法、离线、T9、numpad。

## English

Short description: Turn your numeric keypad into a phone-layout nine-key input method for offline Chinese Pinyin and English.

Description:

Numpad9 brings a familiar phone-style nine-key layout to your Windows numeric keypad. Keys follow their physical positions: the PC's 7, 8, and 9 become the phone's 1, 2, and 3; the PC's 1, 2, and 3 become the phone's 7, 8, and 9. Your hands can follow the phone layout instead of the numbers printed on your keyboard.

Numpad9 runs as a Windows input method with local candidates and text confirmation. It uses a bundled offline Chinese Pinyin dictionary and provides English nine-key candidates. Switch through the Windows input-method menu and open the help window for the layout and controls. The bundled dictionary has limited coverage; this version does not offer cloud predictions or personal frequency learning.

Requires an x64 PC with Windows 10 version 1809 or later, or Windows 11, and a numeric keypad. Keys are identified by physical position with Num Lock either on or off. Setup requires administrator approval. Enable Numpad9 in the Chinese language keyboard options, then use Win+Space to switch. ARM64, 32-bit Windows, and Windows S mode are not supported in this version.

Typing is processed locally and is not uploaded. There are no advertisements or telemetry. Remove the application through Windows Settings. Numpad9 is distributed under GPL-3.0 with version-matched corresponding source and dictionary attribution.

## Assets and account fields still required

- Real developer account, legal publisher, contact/support, category, markets/pricing, age questionnaire, license terms and versioned source URL. A reasonable proposed category is Utilities & tools; choose the available matching Partner Center value.
- Actual privacy-policy text in the EXE product Properties field, or a publicly reachable privacy-policy URL. Use real support information and remove template placeholders; the installed policy also identifies the legal app publisher. Declare access to personal information because the input method processes typed text even though it does not transmit it.
- At least one **real Windows application screenshot**; prepare four: setup/help and layout, Chinese candidates in an actual x64 text app, English input, Windows input-method selection. Capture from the running application, including through Windows automation; redact personal information.
- Store box art (1:1) and any desired poster art, exported from the app's original logo to the current Partner Center dimensions.
- EXE package architecture: x64 only. Languages: Chinese and English application interface. Minimum OS: Windows 10 1809. Installation parameters are in README.md.
- Applicable license terms should identify GPL-3.0 and link to full corresponding source; dictionary attribution and its own license must remain available. Do not use a restrictive default EULA that contradicts the source license.

## Reviewer notes draft (English)

Numpad9 is an offline Windows TSF input method for numeric keypads. The package supports x64 Windows 10 1809+ and Windows 11 and includes both x86 and x64 text-service components for applications of either bitness. ARM64 and 32-bit Windows are unsupported. Administrative UAC is required for system input-method registration; setup performs no downloads and installs no service or driver.

Install with /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /RESTARTEXITCODE=3010. Open Numpad9 from Start for instructions. Add/select it in Windows Chinese (Simplified, China) language keyboard settings and switch using Win+Space. Use a physical numeric keypad with Num Lock either on or off. The physical PC top row 7/8/9 maps to phone keys 1/2/3; bottom row 1/2/3 maps to 7/8/9. Test Chinese and English composition in both x64 and x86 text applications. Password contexts do not provide conversion. No account is required; input is local. Uninstall from Windows Settings. Close applications using the IME and restart if needed to remove locked DLLs. No default input-method override or startup entry is installed.

Keep the reviewer notes within Partner Center's 2,000-character limit. Include only actual product behavior and installation instructions.
