# Numpad9 · 九键输入

将电脑数字小键盘按**手机九键的物理位置**映射，支持离线中文拼音和英文候选。GitHub 提供 EXE 版本；Microsoft Store 使用 MSIX 版本。当前源码版本为 `0.1.0`，MSIX 输入服务的 Microsoft 授权及商店认证尚未完成。

| 手机位置 | 功能 | 电脑小键盘键帽 |
| --- | --- | --- |
| 1 / 2 / 3 | 逗号 / ABC / DEF | 7 / 8 / 9 |
| 4 / 5 / 6 | GHI / JKL / MNO | 4 / 5 / 6 |
| 7 / 8 / 9 | PQRS / TUV / WXYZ | 1 / 2 / 3 |

输入「你好」的拼音 `nihao`，手机逻辑编码是 `64426`，实际按电脑小键盘 **6 4 4 8 6**。Num Lock 开启或关闭时均按物理位置转换；主键盘数字与独立方向键保留各自功能。

## EXE 版本使用

安装后，从开始菜单打开 Numpad9，点击「切换到 Numpad9」，或通过 `Win + 空格` 选择该输入法。若未显示，请在 Windows 语言设置添加「中文（简体，中国）」，安装后重新登录再试。应用内提供手机键位图、真实 TSF 练习文本框、中英文说明和隐私政策。

- `Space` / `Enter`：确认候选；`Esc`：取消并恢复原选中文字；`Backspace`：删除拼音编码。
- 独立方向键或小键盘 `*`：选择候选；小键盘 `+` / `−`：翻页。
- 小键盘 `/` 或 `Ctrl + Shift + E`：切换拼音和英文模式。
- 电脑小键盘 `7`（手机 1）：逗号；小数点：句号；`0`：确认当前候选，空闲时输入空格。
- 逐词确认可组成句子。未知词可以拆成已有的词或单字；当前没有云预测、个人词频学习或拼音简拼。

关闭帮助窗口后输入法仍然可用。切回其他输入法即可恢复普通小键盘输入。输入服务直接通过 Windows TSF 写入目标文本控件；没有全局键盘钩子、剪贴板替换、模拟按键、驱动或后台联网服务。密码和受保护的上下文不提供九键转换。

工程目标为 Windows 10 1809+ / Windows 11 **x64**，同时提供 x86 DLL 用于 32 位文本应用。暂不支持 ARM64、32 位 Windows 或 S 模式。当前按项目要求使用代码审查、跨平台核心测试和 Windows 自动化构建检查；手动兼容性清单供后续维护参考。

## 构建

Windows 上安装 Visual Studio 2022 C++ 桌面开发组件、Windows SDK、CMake 和 Inno Setup 6.3+：

```powershell
./scripts/Build-Windows.ps1
./scripts/Build-Installer.ps1 -UnsignedDevelopment
```

输出 `artifacts/Numpad9-0.1.0-x64-UNSIGNED-TESTING.exe`，是可离线安装的开发测试包。该包未经可信签名，不能提交 Microsoft Store。GitHub Actions 工作流会构建两种位数、运行核心和 COM/TSF 生命周期测试，并上传测试安装包。

macOS / Linux 上可验证与 Windows 无关的输入引擎：

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
```

词库含 183,235 条中文读音和英文词条。来源快照、SHA256、生成脚本与许可随仓库提供；详情见 [词库说明](docs/dictionary.md)。

## MSIX / Microsoft Store

商店产品为「Numpad9 九键输入法」，Store ID `9N82M2S3QS4C`。商店发布后使用 [Microsoft Store 下载链接](https://apps.microsoft.com/detail/9N82M2S3QS4C)。创建产品记录不代表已经发布；当前还不能从商店安装。

MSIX 版本使用 Windows 的 `Windows.UI.Input.Preview.Text` 输入服务 API，要求 Windows 11 24H2（build 26100）或更高版本。它复用同一离线词库和九键引擎，通过当前输入控件的编辑会话维护组合文字。该版本不运行 EXE 安装器、不注册传统 TSF DLL，不需要购买商店代码签名证书；Microsoft Store 负责给获准发布的 MSIX 签名。

这个 API 需要 Microsoft 提供与真实包身份匹配的 LAF 访问配置，并确认输入配置的注册、激活及卸载方式。商店还需批准 TextInputMethod 自定义能力。缺少授权时程序会禁用输入服务；CI 的开发身份包不能作为已完成的商店版本发布。

```powershell
# 编译、验证开发身份包；Windows SDK 必须包含实际输入服务接口。
./scripts/Build-Msix.ps1 -DevelopmentIdentity
```

真实包身份位于 `packaging/msix/store-identity.json`。审批流程和正式构建参数见 [MSIX 发布说明](docs/store/msix.md)、[微软 API 申请说明](docs/store/laf-request.md)。访问配置保存在本机 `private/` 中，不能填写猜测的令牌或输入配置标识。

## GitHub EXE 发布

EXE 保留 Windows 10/11 x64 的原生 TSF 路线，带 x86 输入组件，用于传统桌面应用。安装器注册系统输入法，升级时使用独立版本目录，卸载撤销注册。GitHub 将提供安装器和版本对应的完整源码压缩包，并附上商店下载链接。未签名 EXE 可能显示 Windows 信任提示；可用 [Mac 签名流程](docs/store/mac-signing.md)另行签名。商店只提交 MSIX。

程序采用 GNU GPL v3；中文词库采用 CC BY-SA 4.0，英文词表采用其原始开放许可。完整许可和署名见 `LICENSE`、`data/NOTICE.txt`、`data/licenses/`。发布二进制时必须提供对应的完整源码及构建脚本，不附加限制这些许可权利的条款。
