# Microsoft Store MSIX 分发

商店版使用独立的 `Numpad9Store.exe`，目标为 Windows 11 24H2（build 26100）及以上的 x64 电脑。应用使用 Windows 文本输入 API；安装、更新和卸载由 Windows 与 Microsoft Store 管理。Microsoft Store 为分发的 MSIX 签名，此路线无需购买商业代码签名证书。

## 包身份与授权

正式身份来自 Partner Center，记录于 `packaging/msix/store-identity.json`：

- 包标识：`14E33A41.Numpad9`
- 发行人证书主题：`CN=BD412261-9548-4F5A-95C1-24B072A9C1CC`
- 发行人显示名称：`Zhiqi Zhang`
- 包家族名称：`14E33A41.Numpad9_1c2s66sz70f52`

包声明 `runFullTrust` 和 `Microsoft.appCategory.textInputMethod_8wekyb3d8bbwe`。TextInputMethod 自定义能力在 Store 提交时申请批准。Microsoft 发放的 LAF 授权包含 `featureId`、`token` 和 `attestation`，运行配置保存于私有的 `private/input-access.json`，不提交到公开源码仓库。

模板为 `packaging/msix/input-access.example.json`。`inputProfile` 是可选的 Windows 输入配置绑定值；留空时，应用读取启用时的当前 Windows 键盘配置。用户在 Numpad9 窗口中启用或停用输入，无需修改默认输入法。

## 打包

Windows 打包工具需要 Visual Studio 2022 或 2026 的 C++ 工具、CMake、Windows SDK 10.0.26100.0 和 PowerShell 7。

```powershell
./scripts/Build-Msix.ps1 -IdentityName '14E33A41.Numpad9' `
    -PublisherDN 'CN=BD412261-9548-4F5A-95C1-24B072A9C1CC' `
    -PublisherDisplayName 'Zhiqi Zhang' `
    -RuntimeConfigurationPath ./private/input-access.json `
    -PolicyTextPath ./docs/store/privacy-policy-msix.md
```

已构建的 Store host 可通过 `-SkipBuild -HostPath '<Numpad9Store.exe 路径>'` 指定。包版本为 `0.1.0.0`，host 产品版本为 `0.1.0`；后续更新应同步提升版本。

输出为 `artifacts/Numpad9-0.1.0.0-x64-STORE-UPLOAD.msix`。包包含 Store host、词典、GPL、词典许可与署名、应用图标、隐私政策和运行配置。正式 MSIX 交给 Partner Center，授权配置及包含授权配置的包不上传到公开 GitHub Release。

macOS 可使用 Microsoft 的 [MSIX Packaging SDK](https://github.com/microsoft/msix-packaging) 的 `makemsix pack -d <包目录> -p <输出.msix>` 打包同一载荷。正式包使用上述 Partner Center 身份，由 Microsoft Store 签名分发。

完整对应源码应包含两种输入实现、构建工具及词典来源，并保留第三方许可。GitHub EXE 与商店 MSIX 的使用方法和最低系统要求分别说明。

## Microsoft 文档

- [MSIX 签名指南](https://learn.microsoft.com/en-us/windows/msix/package/sign-msix-package-guide)
- [分发路线](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/choose-distribution-path)
- [能力声明](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations)
- [上传包格式](https://learn.microsoft.com/en-us/windows/msix/package/packaging-uwp-apps)
