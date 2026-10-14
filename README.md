# Numpad9 · 九键输入

把电脑数字小键盘按**手机九键的物理位置**映射，离线输入中文拼音和英文。

| 手机位置 | 功能 | 电脑小键盘键帽 |
| --- | --- | --- |
| 1 / 2 / 3 | 逗号 / ABC / DEF | 7 / 8 / 9 |
| 4 / 5 / 6 | GHI / JKL / MNO | 4 / 5 / 6 |
| 7 / 8 / 9 | PQRS / TUV / WXYZ | 1 / 2 / 3 |

输入「你好」的拼音 `nihao`，手机逻辑编码为 `64426`，实际按电脑小键盘 **6 4 4 8 6**。Num Lock 开启或关闭均按物理位置识别；主键盘数字与独立方向键保持原有功能。

## 下载与启用

| 版本 | 系统要求 | 下载 | 启用方式 |
| --- | --- | --- | --- |
| GitHub EXE | Windows 10 1809+ / Windows 11，x64 | [v0.1.0 预发行](https://github.com/shoutoutuoadi325/Numpad9/releases/tag/v0.1.0) | 安装后从开始菜单打开 Numpad9，点击「切换到 Numpad9」或使用 `Win + 空格` |
| Microsoft Store MSIX | Windows 11 24H2+，x64 | [商店页面](https://apps.microsoft.com/detail/9N82M2S3QS4C) | 打开 Numpad9，点击「启用九键输入」，再将焦点放到普通文本框 |

GitHub 下载选择 `Numpad9-0.1.0-x64-UNSIGNED.exe`，页面同时提供对应源码和 `SHA256SUMS.txt`。该 EXE 未签名，Windows 可能显示未知发布者提示；安装需要管理员授权。若输入法未出现在列表中，请添加「中文（简体，中国）」语言并重新登录。

商店版本已提交认证，可用性以商店页面为准。Microsoft Store 管理 MSIX 的签名、更新和卸载。两种版本均需要实体数字小键盘；当前发行面向 x64 Windows，不提供 ARM64、32 位 Windows 或 S 模式版本。

## 使用

应用内提供中英文键位指南、练习文本框及隐私说明。

- `Space` / `Enter`：确认候选；`Esc`：取消；`Backspace`：删除一个输入编码。
- 小键盘 `*`：选择下一候选；`+` / `−`：翻页；`/`：切换中文与英文。
- 小键盘 `7`（手机 1）：逗号；小数点：句号；`0`：确认候选，空闲时输入空格。
- 逐词确认可组成句子。未知词可拆成已有词或单字；应用不提供云预测、个人词频学习或拼音简拼。

EXE 版本切回其他输入法后恢复普通小键盘；关闭帮助窗口不会停用输入法。MSIX 版本通过「停用九键输入」恢复普通小键盘，托盘菜单可退出应用。

## 文档

- [构建与项目结构](docs/build.md)
- [GitHub EXE 发布](docs/release-github.md)
- [Microsoft Store MSIX 打包](docs/store/msix.md)
- [词库来源、重建与许可](docs/dictionary.md)
- [可选的 EXE 代码签名](docs/signing.md)
- 实现说明：[传统 TSF](src/ime/README.md)、[MSIX 输入服务](src/msix/README.md)

## 隐私、许可与支持

输入和候选转换在本机内存中处理，不上传按键或文字，不保存输入历史，没有广告、遥测或后台联网服务。密码和受保护的输入上下文不提供转换。商店版完整政策见 [隐私政策](docs/store/privacy-policy-msix.md)；EXE 的发行人和支持信息随安装包政策提供。

应用采用 [GNU GPL v3](LICENSE)。中文词库采用 CC BY-SA 4.0，英文词表保留其原始开放许可；完整署名见 [data/NOTICE.txt](data/NOTICE.txt) 和 [data/licenses](data/licenses)。分发二进制时应同时提供对应的完整源码与构建脚本。

问题反馈：[GitHub Issues](https://github.com/shoutoutuoadi325/Numpad9/issues)。
