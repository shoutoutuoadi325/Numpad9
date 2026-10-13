# Microsoft Store 发布准备

当前版本 `0.1.0` 提供原生 TSF 输入法、离线 EXE 安装方案和发布检查。开发流程采用代码审核、Windows 自动构建与测试；按项目要求不设置人工实机测试门槛。Microsoft Store 的最终认证由 Microsoft 完成。

选择 Microsoft Store 的 **EXE/MSI 传统桌面应用**通道。TSF 输入法需要两种位数的 COM 注册，安装器以管理员权限注册到系统中。此版本面向 Windows 10 1809（build 17763）及以上、Windows 11 **x64**；安装器明确拒绝 ARM64 和 32 位 Windows。x64 Windows 中的 32 位应用由附带的 x86 TSF DLL 支持。

## 本地构建与签名

Windows 构建机需要 Visual Studio 2022 的“使用 C++ 的桌面开发”、Windows SDK、CMake、Inno Setup 6.3 或更高版本。

建议使用 PowerShell 7 执行构建脚本。Inno Setup 版本中包含官方 `ChineseSimplified.isl` 时安装向导提供简体中文；旧版本缺少该翻译时使用英文向导，应用本身仍提供中英文界面。Store 静默安装不显示向导。

```powershell
./scripts/Build-Windows.ps1
./scripts/Build-Installer.ps1 -UnsignedDevelopment
```

生成的 `artifacts/Numpad9-0.1.0-x64-UNSIGNED-TESTING.exe` 仅用于自动构建验证，不能作为受信签名发行包提交。GitHub 工作流默认生成这个包，并执行自动安装、实际主程序窗口截图和卸载检查。

正式候选构建需要发行人的真实证书和信息。使用受 CA 信任、符合 Microsoft Trusted Root Program 的代码签名证书；自签名证书不能满足本路线。脚本使用 CurrentUser 的 `My` 证书存储和精确证书指纹，不接受 PFX 密码作为命令行参数。硬件密钥或云签名供应商如果不能以该证书存储方式使用，需要接入其专用签名工具，保持相同的验证门槛。

```powershell
$env:NUMPAD9_SIGNTOOL = '<Windows SDK signtool.exe 绝对路径>'
$env:NUMPAD9_CERT_THUMBPRINT = '<你的 CA 代码签名证书 SHA1 指纹>'
$env:NUMPAD9_TIMESTAMP_URL = '<证书供应商 RFC3161 时间戳服务 URL>'
./scripts/Build-Installer.ps1 -Publisher '<应用的真实发行人名称>' `
    -Contact '<真实支持联系方式>' -SourceUrl '<本版本完整对应源码的 HTTPS 地址>'
```

`Sign-File.ps1` 给载荷加 SHA256 时间戳签名。Inno 的 `SignTool` 和 `SignedUninstaller=yes` 同时签名安装器与生成的卸载程序，不能仅在编译后给外层 EXE 签名。正式文件名依然是 `SIGNED-CANDIDATE`，完成后续检查才可以提交。

也可以在 Mac 上使用供应商的硬件/云密钥与 osslsigncode 签名，见 [mac-signing.md](mac-signing.md) 和 `scripts/Sign-Mac.py`。该路线先签 payload 和精确 Inno 卸载缓存，再重编译并签外层安装器；Windows CI 无需保存私钥。

安装包内的隐私政策会填入提供的真实发行人和联系方式，并包含 `SOURCE.txt` 的对应源码地址。Partner Center 的 EXE 产品属性提供隐私政策文本字段，可以直接填写实际政策全文；公开 HTTPS 政策网页是另一种提交方式，并非文本方式的前置要求。用于 Store 的政策文本应准确说明输入在本机处理、不会上传以及实际支持渠道，不能包含模板占位符。安装器中的发行人身份仍必须真实填写。生成完整对应源码归档（包括原始词典、重建工具、原生源码和构建脚本）：

```powershell
./scripts/Build-SourceArchive.ps1
```

将 `artifacts/Numpad9-0.1.0-source.zip` 与该版本安装器一起发布，并保留许可和词典署名。以最后构建的源码状态生成归档，不要只提供尚未推送新代码的仓库地址。

## 安装、升级和卸载

Partner Center 填写的静默参数：

```text
/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /RESTARTEXITCODE=3010
```

安装包含 x64 主程序、x64/x86 输入法及只读词典，不下载任何运行时或词库。C++ 使用静态运行时。安装可能出现 UAC；静默安装不打开主程序，不重启机器，不修改用户默认输入法，也不添加开机启动。用户通过 Windows 的语言设置启用 Numpad9，通过 Win+Space 切换。

目录由系统 Program Files 路径确定：

```text
Program Files\Numpad9\versions\0.1.0\Numpad9.exe
Program Files\Numpad9\versions\0.1.0\Numpad9Ime.dll
Program Files\Numpad9\versions\0.1.0\lexicon.tsv
Program Files (x86)\Numpad9\versions\0.1.0\Numpad9Ime.dll
Program Files (x86)\Numpad9\versions\0.1.0\lexicon.tsv
```

词典继承 Program Files 的只读权限，Windows app container 可读取。安装器从正确位数的 `regsvr32` 分别注册 x86 和 x64。同一 CLSID/profile 在输入法列表中表示一个输入法。

升级使用新的版本目录，不覆盖其他进程已经加载的 DLL，也不强制结束用户应用。旧进程在退出前可能继续运行旧版输入法。注册失败时撤销新注册并尝试恢复先前的两种位数注册，详细结果写入 setup log。相同版本再次安装被拒绝；修复需先卸载、关闭使用该输入法的应用、按需重启，再安装；修改后重新发布必须提升版本号。不要通过旧版安装器降级。

卸载通过 DLL 导出函数撤销 COM、TSF profile 和 category 注册，仅当登记路径仍属于这个版本时执行，避免破坏较新版本的注册。被其他进程锁定的文件安排在重启后删除。因此卸载检查必须包含重启后的文件、快捷方式、程序列表和注册清理。保留旧版目录以支持已加载进程，最终卸载通过累计的安装日志清除它们。

```powershell
& "$env:ProgramFiles\Numpad9\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

## 发布门槛

1. 完成代码审核、Windows x64/x86 自动构建、核心测试与 DLL COM 生命周期测试；保留自动检查输出。
2. [qa-checklist.md](qa-checklist.md) 和 [qa-report.template.json](qa-report.template.json) 是可选的内部参考。人工实机测试不是本次发布的要求；若附上报告，只填写实际执行的检查，不将未执行项目标记通过。
3. 真实发行人、支持联系方式、实际提交的隐私政策文本或公开政策网址、完整对应源码网址、版本化且不可替换的 HTTPS 安装包网址必须由开发者提供。
4. 签名证书链的根指纹需独立核对 Microsoft Trusted Root Program，不允许用本机添加的开发信任根冒充公有 CA。
5. 运行发布检查。`InstalledRoot` 指在 Windows 上从该候选包实际安装得到的目录，必须包含签名卸载程序。

```powershell
./scripts/Validate-Release.ps1 -Installer '<已签名候选 EXE>' `
    -InstalledRoot "$env:ProgramFiles\Numpad9" -Publisher '<应用真实发行人>' `
    -Contact '<真实联系信息>' -PolicyTextPath '<实际提交到 Partner Center 的 UTF-8 政策文件>' `
    -InstallerUrl '<含 0.1.0 的不可变 EXE HTTPS URL>' `
    -SourceUrl '<本版本完整对应源码 HTTPS URL>' `
    -TrustedProgramRootThumbprints '<已核对的公有 CA 根 SHA1 指纹>'
```

默认情况下签名证书主体的 SimpleName 应与应用发行人一致。如果使用 SignPath Foundation 等经授权的第三方签名流程，证书主体可能是基金会，而产品发行人仍是实际开发者。此时明确增加 `-CertificateSubjectName '<实际签名证书 SimpleName>'`；这不会放宽证书链、哈希、时间戳、发行元数据或任何其他检查，也不代表该签名计划一定接受本项目。

隐私文本路线使用 `-PolicyTextPath`，检查文件非空、没有未填写的模板字段，并在报告中记录原文件 SHA256；发布前应核对文件全文与 Partner Center 保存的文本一致。若使用公开网页，改为 `-PrivacyUrl '<隐私政策 HTTPS URL>'`；提供两者时分别检查。隐私文本路线不要求另建网页，也不推断尚未确定的法律发行人身份。

检查会拒绝无效/缺失签名、无时间戳、自签名证书、发行人不匹配、错位数 DLL、缺失安装后的卸载程序。它还核对安装载荷与构建载荷、版本元数据、实际下载的安装器哈希、提供的政策文本哈希或公开政策中的发行人/联系方式；源码 URL 至少需要能访问。安装验证可在 Windows CI/自动化环境完成。可选 `-QaReport` 不要求人工实机项目全部通过；若提供报告，会核对其版本与安装包哈希。网络或 CA 撤销检查失败时停止发布。此检查不替代 Microsoft 的安全与内容认证。

按照 [listing.md](listing.md) 准备中英文介绍、真实应用截图和 Store logo，完成 Partner Center 开发者账号、名称预留、年龄评级、市场及定价、隐私与产品声明。由于输入法会处理用户正在输入的文本，应如实声明访问个人信息，并在 EXE 产品属性中提供完整隐私政策文本，或提供公开可访问的政策 URL；“没有上传”不等于“没有访问”。GPL-3.0 源码交付和词典第三方署名需与每个发布版本匹配。

## 官方依据（2026-10-13 核对）

- [EXE/MSI 包要求](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-package-requirements)：离线、静默、所有 PE 受信签名、版本化 HTTPS 地址。
- [认证流程](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-certification-process)及[手动验证](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/manual-package-validation)：安装、程序列表、卸载、恶意软件和隐私审查。
- [提交清单](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/create-app-submission)：账号、架构、参数、评级、截图、logo、许可条款。
- [TSF 的 64 位要求](https://learn.microsoft.com/en-us/windows/win32/tsf/64-bit-platform-considerations)：同 CLSID、分别注册、分别安装到两种 Program Files。
- [自定义 IME 要求](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements)：TSF、app container、只读词典、默认键盘由用户选择。
- [Store 政策](https://learn.microsoft.com/en-us/windows/apps/publish/store-policies)及 [SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool)。
- [Inno SignedUninstaller](https://jrsoftware.org/ishelp/topic_setup_signeduninstaller.htm)和[签名工具](https://jrsoftware.org/ishelp/topic_setup_signtool.htm)：签名安装器与生成的卸载程序。
