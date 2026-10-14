# GitHub EXE 发布

GitHub 分发原生 TSF EXE，当前首个公开版本为 **v0.1.0 预发行**。发行人是 **Zhiqi Zhang**，支持渠道是 [GitHub Issues](https://github.com/shoutoutuoadi325/Numpad9/issues)。此路线不需要商业代码签名证书；资产文件名和发布说明明确标记未签名，Windows 可能显示未知发布者提示。

每次发布包含：

- `Numpad9-<version>-x64-UNSIGNED.exe`：真实发行元数据的离线安装器，包含 x64 主程序和 x64/x86 TSF DLL。
- `Numpad9-<version>-source.zip`：与该 tag 对应的完整源码、词典原始来源、许可、重建工具及构建脚本。
- `SHA256SUMS.txt`：以上两个文件的 SHA256。

测试包仍叫 `UNSIGNED-TESTING.exe`。不要把测试包重命名为公开包；公开包需要实际发行人、支持网址和对应源码下载地址，并再次检查它自己的安装与卸载。

## 自动发布

`.github/workflows/windows.yml` 在 `main` 推送和 PR 时只构建检查。在 `v<major>.<minor>.<patch>` tag 推送时，或手动 `workflow_dispatch` 提供 `release_tag` 时，运行公开发布 job。已有 tag 必须指向本次实际 checkout 并匹配源码版本。首次发布可以在手动运行界面选择 `main`、填写尚不存在的 `v0.1.0`；准备阶段只记录意图，所有自动检查通过后才将新 tag 创建到精确验证的提交。源码不能有未提交修改。

EXE 与发布 job 必须使用同一次运行的相同 checkout 提交。发布 job 依赖 EXE 自动检查成功，下载同一次运行的已构建 payload，重新编译真实发行元数据的 `-UNSIGNED.exe`，并对**这个实际公开安装器**运行完整自动烟测。检查包括核心与 COM 生命周期测试、静默安装、x64/x86 系统注册和实际 TSF 激活、真实窗口、卸载及 CI 语言偏好恢复；没有人工实机 QA 门槛。

仅发布 job 有 `contents: write`。它在自动检查通过后核对远端 main 未变化、已有 tag 没有指向其他提交；需要时将新 tag 创建到验证的 SHA。随后创建 GitHub 草稿，上传三个公开资产，下载回来验证 SHA256，再次核对 tag/main，最后才公开预发行。已有同名 release 不覆盖；发生错误时不公开草稿。MSIX job 独立运行，不作为 GitHub EXE 发布依赖。

已有 tag 的手动发布可以选择该 tag 并填写相同 `release_tag`；首次发布选择 main。不同提交不得共用同一个版本 tag，已有 release 不覆盖。若 main 在检查期间有新提交，需要重新运行验证当前提交。此工作流发布预发行；后续稳定发行需明确调整发布策略。

## 本地准备

本地准备应在 Windows 上 checkout 已有版本 tag；首次缺失 tag 的创建仅允许由 main 上显式手动运行的 GitHub 工作流在检查通过后执行：

```powershell
./scripts/Prepare-GitHubRelease.ps1 -Tag v0.1.0 -Repository shoutoutuoadi325/Numpad9
./scripts/Build-Windows.ps1
./scripts/Build-Installer.ps1 -GitHubUnsignedRelease -Publisher 'Zhiqi Zhang' `
    -Contact 'https://github.com/shoutoutuoadi325/Numpad9/issues' `
    -SupportUrl 'https://github.com/shoutoutuoadi325/Numpad9/issues' `
    -SourceUrl 'https://github.com/shoutoutuoadi325/Numpad9/releases/download/v0.1.0/Numpad9-0.1.0-source.zip'
./scripts/Test-Installer.ps1 -Installer ./artifacts/Numpad9-0.1.0-x64-UNSIGNED.exe
./scripts/Prepare-GitHubRelease.ps1 -Tag v0.1.0 -Repository shoutoutuoadi325/Numpad9 -Phase Finalize
```

`Test-Installer.ps1` 只用于 disposable Windows 自动检查环境，会临时登记中文用户语言偏好并恢复原设置。一般用户只运行 EXE 安装器；安装器不会替用户修改默认输入法或下载语言包。

签名路线与此分开：`Validate-Release.ps1` 对受信签名包保持严格检查，未签名 GitHub 发行不会冒充已签名或通过 Microsoft Store 认证。Microsoft Store 只走独立 MSIX 路线，发布说明保留 [Store 链接](https://apps.microsoft.com/detail/9N82M2S3QS4C)，但在实际上架之前不声称能从商店下载。
