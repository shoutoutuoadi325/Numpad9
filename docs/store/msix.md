# Microsoft Store MSIX 打包

商店版使用 `Numpad9Store.exe` 和 Windows 文本输入 API，面向 Windows 11 24H2（build 26100）及以上的 x64 电脑。Windows 与 Microsoft Store 管理安装、更新和卸载，Store 为分发的 MSIX 签名，无需购买商业代码签名证书。使用方法见 [README](../../README.md)，产品政策见 [隐私政策](privacy-policy-msix.md)。

## 身份与输入授权

正式包身份以 `packaging/msix/store-identity.json` 为准，包括包名称、发行人证书主题、显示名称及包家族名称。

包声明 `runFullTrust` 和 `Microsoft.appCategory.textInputMethod_8wekyb3d8bbwe`，在 Store 提交时说明能力用途并申请批准。LAF 授权由 Microsoft 按包家族名称发放；申请入口为 [Text Input Service 访问申请表](https://forms.office.com/r/5tb5LdcHZa)。LAF 授权与商店能力批准分别处理。

运行配置参考 `packaging/msix/input-access.example.json`，实际值保存于忽略的 `private/input-access.json`：

- `featureId`、`token`、`attestation`：Microsoft 发放的三项授权值。
- `inputProfile`：可选的 Windows 输入配置绑定；留空时读取启用时的当前键盘配置。

授权配置不提交公开源码，也不随 GitHub Release 分发。用户在应用中启用或停用九键输入，无需修改默认输入法。

## Windows 打包

需要 PowerShell 7、CMake、Windows SDK 10.0.26100.0，以及 Visual Studio 2022 或 2026 的 C++ 工具。

```powershell
$identity = Get-Content -Raw packaging/msix/store-identity.json | ConvertFrom-Json
./scripts/Build-Msix.ps1 -IdentityName $identity.identityName `
    -PublisherDN $identity.publisherDN `
    -PublisherDisplayName $identity.publisherDisplayName `
    -RuntimeConfigurationPath ./private/input-access.json `
    -PolicyTextPath ./docs/store/privacy-policy-msix.md
```

已构建的 host 可通过 `-SkipBuild -HostPath '<Numpad9Store.exe 路径>'` 指定。`-Version` 使用四段包版本并以 `.0` 结尾，与 host 产品版本匹配。

输出为 `artifacts/Numpad9-<version>-x64-STORE-UPLOAD.msix`。包包含 Store host、词典、许可与署名、图标、隐私政策和授权配置。正式包上传 Partner Center，完整对应源码保留在版本固定的 GitHub 提交或发行归档中。

macOS 可使用 Microsoft 的 [MSIX Packaging SDK](https://github.com/microsoft/msix-packaging) 对同一载荷运行 `makemsix pack -d <包目录> -p <输出.msix>`。

Microsoft 文档：[MSIX 签名](https://learn.microsoft.com/en-us/windows/msix/package/sign-msix-package-guide)、[能力声明](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations)、[上传包格式](https://learn.microsoft.com/en-us/windows/msix/package/packaging-uwp-apps)。
