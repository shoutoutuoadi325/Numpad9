# Numpad9 · 九键输入

Windows 原生 TSF 输入法，将电脑数字小键盘按**手机九键的物理位置**映射，支持离线中文拼音和英文候选。版本 `0.1.0` 是开发候选版本，尚未完成 Windows 实机认证或 Microsoft Store 审核。

| 手机位置 | 功能 | 电脑小键盘键帽 |
| --- | --- | --- |
| 1 / 2 / 3 | 逗号 / ABC / DEF | 7 / 8 / 9 |
| 4 / 5 / 6 | GHI / JKL / MNO | 4 / 5 / 6 |
| 7 / 8 / 9 | PQRS / TUV / WXYZ | 1 / 2 / 3 |

输入「你好」的拼音 `nihao`，手机逻辑编码是 `64426`，实际按电脑小键盘 **6 4 4 8 6**。Num Lock 开启或关闭时均按物理位置转换；主键盘数字与独立方向键保留各自功能。

## 使用

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

## 发布

采用 Microsoft Store **EXE/MSI** 应用通道，以离线 EXE 安装器注册两种位数的系统输入法。输入法 DLL 安装在 Program Files 中的版本目录；不改系统默认输入法、不添加启动项，更新时不覆盖其他应用已加载的 DLL。签名构建同时签名程序、DLL、安装器和卸载器；发布检查验证可信证书链、文件一致性、版本和公开发布资料。

完整步骤和未完成门槛见 [Microsoft Store 发布准备](docs/store/README.md)、[Windows QA 清单](docs/store/qa-checklist.md)、[隐私政策草稿](docs/store/privacy-policy.md)和[中英文商店文案](docs/store/listing.md)。需提供真实签名证书、公开隐私及源码网址、不可变的安装包网址和真实 Windows 截图，再完成 Partner Center 提交和微软认证。

程序采用 GNU GPL v3；中文词库采用 CC BY-SA 4.0，英文词表采用其原始开放许可。完整许可和署名见 `LICENSE`、`data/NOTICE.txt`、`data/licenses/`。发布二进制时必须提供对应的完整源码及构建脚本，不附加限制这些许可权利的条款。
