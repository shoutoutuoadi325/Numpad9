# Numpad9 — Text Input Service access request

Product: **Numpad9**  
Requested API: **Text Input Service**, `Windows.UI.Input.Preview.Text`  
Requested platform: **Windows 11 version 24H2 or later**, minimum build **10.0.26100.0**  
Microsoft Store capability: `Microsoft.appCategory.textInputMethod_8wekyb3d8bbwe`

## English — product and API use case

Numpad9 is an offline Chinese Pinyin and English input method that turns a PC numeric keypad into a phone-style nine-key layout. The physical top row, marked 7/8/9 on a PC keypad, represents phone keys 1/2/3. The middle row remains 4/5/6, and the physical bottom row, marked 1/2/3, represents phone keys 7/8/9. This preserves the positions familiar to people who type with a phone's nine-key keyboard. Users enter a digit sequence, choose a Chinese or English candidate, and commit the chosen text to the focused text control.

We request access to the Text Input Service API in `Windows.UI.Input.Preview.Text` for the Numpad9 MSIX app, targeting Windows 11 version 24H2, build 26100, or later. Its requested use is:

- Receive activation, deactivation and focus notifications so input conversion runs only while the user has selected Numpad9 and an eligible text control is focused.
- Receive keyboard events for the physical numeric keypad and the controls needed to edit, select, commit or cancel the current composition. Unrelated keys and application shortcuts remain available to the focused application.
- Use edit sessions to start and update Numpad9's current composition, inspect only the insertion/selection range required for that operation, and replace that range with the candidate the user selects. This is needed to deliver Chinese Pinyin and English conversion directly in the user's focused application rather than requiring a separate text box and copy/paste.
- Clear pending input when focus changes, the input service deactivates, or the user cancels composition. Respect the focused control's input scope, privacy settings and disabled input-method features; exclude password and protected fields.

The requested access is limited to the current input interaction. Numpad9 does not scan documents, enumerate other applications' text controls, build a typing history, or read unrelated document content. Candidate conversion uses a bundled read-only dictionary and runs entirely locally. Keystrokes and input text exist only in the active composition's memory and are not logged, retained as typing history, used for telemetry, or transmitted. The app has no networking, advertising, analytics or cloud conversion service. Confirmed text is delivered only to the user-selected target application, which handles that text under its own privacy policy.

Public product source: [Numpad9 repository](https://github.com/shoutoutuoadi325/Numpad9). Supporting source reference: [Numpad9 pull request #1](https://github.com/shoutoutuoadi325/Numpad9/pull/1). These links provide product evidence; this request does not assert that an access token or capability approval has already been granted.

## 中文 — 产品用途与 API 使用理由

Numpad9 是一款完全离线的中文拼音和英文输入法，将电脑实体小键盘转换为手机九键布局。电脑小键盘最上排印刷的 7/8/9 对应手机九键的 1/2/3，中间排仍为 4/5/6，最下排印刷的 1/2/3 对应手机九键的 7/8/9。用户按照手机九键熟悉的位置输入数字序列，选择中文或英文候选，并将选中的文字输入当前有焦点的文本控件。

我们申请为 Numpad9 的 MSIX 应用使用 `Windows.UI.Input.Preview.Text` 命名空间中的 Text Input Service API，目标系统为 Windows 11 24H2、内部版本 26100 或更高版本。申请的用途限于：

- 接收输入服务启用、停用和文本焦点事件，使转换仅在用户选中 Numpad9 且合适的文本控件获得焦点时进行。
- 接收实体小键盘按键，以及编辑、选择、确认或取消当前组合输入所需的控制键。无关按键和应用快捷键交由当前应用处理。
- 通过编辑会话创建和更新 Numpad9 当前的组合输入，仅查看本次操作必需的插入点或选中范围，并以用户选择的候选替换该范围。这使中文拼音和英文转换直接发生在用户当前使用的文本应用中，无需借助独立文本框再复制粘贴。
- 在焦点改变、输入服务停用或用户取消输入时清空未确认输入。遵守目标控件的输入范围、隐私设置和禁用输入法功能的要求；在密码和受保护字段中不进行转换。

申请的访问范围仅限当前输入操作。Numpad9 不扫描文档、不枚举其他应用的文本控件、不建立输入历史，也不读取无关文档内容。候选转换使用只读内置词库，完全在本地进行。按键和输入文字仅存在当前组合输入的内存中，不写入日志、不保存为输入历史、不用于遥测，也不向外传输。应用没有网络通信、广告、统计分析或云端转换服务。确认的文字只交给用户选择的目标应用，由目标应用按其自身隐私政策处理。

公开产品源代码：[Numpad9 仓库](https://github.com/shoutoutuoadi325/Numpad9)。相关源代码参考：[Numpad9 Pull Request #1](https://github.com/shoutoutuoadi325/Numpad9/pull/1)。这些链接用于说明产品，不表示已经取得 API 访问令牌或能力审批。

## Microsoft Store custom capability explanation — English

Numpad9 requests `Microsoft.appCategory.textInputMethod_8wekyb3d8bbwe` to provide a user-selected Chinese Pinyin and English input method using a physical numeric keypad in phone-style nine-key positions. The Text Input Service API requires focus notifications, keyboard events and edit sessions to maintain the current composition and commit the user-selected candidate into the focused text control. Access is limited to the current insertion/selection and Numpad9 composition. Password/protected fields and controls that disable input-method features are excluded. Pending input is cleared on focus changes, cancellation and deactivation. Conversion is entirely offline with a bundled read-only dictionary; no keystroke logging, typing history, telemetry, analytics or input transmission occurs.

## Microsoft Store 自定义能力说明 — 中文

Numpad9 申请 `Microsoft.appCategory.textInputMethod_8wekyb3d8bbwe`，用于提供用户主动选中的中文拼音和英文输入法，将实体小键盘位置映射为手机九键位置。Text Input Service API 的焦点事件、键盘事件和编辑会话用于维护当前组合输入，并将用户选择的候选写入当前有焦点的文本控件。访问仅限当前插入点或选中范围及 Numpad9 组合输入。密码、受保护字段和禁用输入法功能的控件不参与转换。在焦点改变、取消输入或服务停用时清空未确认输入。转换完全离线，使用只读内置词库，不记录按键、不建立输入历史、不进行遥测或统计分析，也不传输输入内容。

## Microsoft requirements and request channel

The API's documented access requirements are distinct: obtain a Limited Access Feature unlock token from Microsoft, and declare the Text Input Method custom capability and request its approval during Microsoft Store submission. Receiving a token does not itself constitute Store capability approval. [TextInputService requirements](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.textinputservice?view=winrt-26100), [custom capability requirements](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations#custom-capabilities).

微软文档要求分别取得 Limited Access Feature 解锁令牌，并在 Microsoft Store 提交时声明 Text Input Method 自定义能力及申请能力审批。取得令牌本身不代表已取得商店能力审批。

Official request channel: [Microsoft LAF Access Token Request Form](https://forms.office.com/r/5tb5LdcHZa). Select **New request** and **Text Input Service** when those choices are presented. Provide the applicant's real name, organization and contact email directly to Microsoft; private contact details and access tokens are not part of this public document.

The documented API contract begins with Windows Desktop Extension SDK **10.0.26100.0**. Relevant API references: [KeyboardInputProcessor](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.keyboardinputprocessor?view=winrt-26100), [TextEditSession](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.texteditsession?view=winrt-26100), [TextBoxInfo](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.textboxinfo?view=winrt-26100), [TextBoxSettings](https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.preview.text.textboxsettings?view=winrt-26100).

Sources checked on 2026-10-13. The text above describes the access being requested.
