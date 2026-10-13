# Microsoft Store MSIX 路线

本项目的 Store 分发使用 **MSIX**；GitHub EXE 保留传统 TSF 安装方式。两种载荷和最低系统要求不同，不能用 EXE 的介绍或截图代替 Store 版本实际行为。

MSIX 使用独立 `Numpad9Store.exe`，目标为 **Windows 11 24H2（build 26100）及以上 x64**。它使用 Windows 文本输入 API，不包含传统 TSF DLL、`registry.dat`、COM 注册程序或安装脚本。Windows 管理 MSIX 安装、更新和卸载。Store 会为最终分发的 MSIX 签名，**此路线无需购买商业代码签名证书**。未签名上传候选不能当作普通网站下载的受信 MSIX 发行包。

## Microsoft 提供的身份与授权

以下三项必须与 **MSIX 产品** Partner Center 产品标识完全一致：包标识名称 `IdentityName`、发行人证书主题 `PublisherDN`、发行人显示名称 `PublisherDisplayName`。已建立的 EXE 产品标识不能自动当作 MSIX 标识；不要根据应用名称或法律姓名猜测这些值。

包声明 `runFullTrust` 和自定义能力 `Microsoft.appCategory.textInputMethod_8wekyb3d8bbwe`，后者需要 Microsoft 授权和 Store 审批。输入访问配置还需要真实的 `featureId`、`token`、`attestation`、`inputProfile`，须由 Microsoft 给出或按其正式授权流程取得。它们不是商业签名证书，不能编造。

空模板位于 `packaging/msix/input-access.example.json`。在本地 `private/input-access.json` 填入真实值，实际授权文件不提交仓库。`.gitignore` 忽略 `private/` 和所有实际 `input-access.json`；源码归档也排除该运行配置。正式包会将配置交付运行时，所以不要在这里放账户密码、私钥或其他无关秘密。

脚本检查四个字段均为非空、无占位符的字符串，复制为包根目录 `input-access.json`，报告只记录哈希。**离线结构检查不能证明授权有效或绑定关系正确。** Microsoft 的授权、实际运行时许可和 Store 能力认证仍是发布条件。`inputProfile` 的配置格式、系统生命周期和授权方式需要以 Microsoft 提供的契约为准，不擅自补写传统 TSF 注册。

## 构建

需要 Visual Studio 2022 C++、CMake、Windows SDK **10.0.26100.0** 和 PowerShell 7。脚本显式选择该 SDK 编译 Store host。

```powershell
./scripts/Build-Msix.ps1 -DevelopmentIdentity
```

此命令使用仅供 CI 的开发身份，不能提交 Store。缺少真实授权配置时，应用保持输入功能禁用并显示状态。打包成功不代表输入授权成功，也不是 Windows 11 实际输入场景的验证结果。

正式上传候选：

```powershell
./scripts/Build-Msix.ps1 -IdentityName '<Partner Center 包标识名称>' `
    -PublisherDN '<Partner Center 发行人证书主题>' `
    -PublisherDisplayName '<Partner Center 发行人显示名称>' `
    -RuntimeConfigurationPath ./private/input-access.json `
    -PolicyTextPath '<与实际 Store 版本相符的 UTF-8 政策文件>'
```

`-PolicyTextPath` 可选，可将已经提交 Store 的实际政策文本随包提供。不要打包旧 TSF 政策模板或未填写占位符的草稿。Store 属性中的政策仍需与新版本处理输入的方式相符。

已构建的 host 可通过 `-SkipBuild -HostPath '<Numpad9Store.exe 绝对路径>'` 指定。正式包同样校验真实身份、授权配置、PE 位数和版本。默认包版本 `0.1.0.0` 与 host 版本 `0.1.0` 匹配，后续版本须一起提升。

输出 `artifacts/Numpad9-0.1.0.0-x64-STORE-UPLOAD.msix`，保留对应 JSON 报告和 SDK 日志。这个上传候选不自行签名，Microsoft Store 在提交处理期间签名。可以向 Partner Center 上传 `.msix`；Visual Studio 生成的 `.msixupload` 是官方推荐的另一种上传格式。

## 检查与分发

`Build-Msix.ps1` 以明确白名单创建全新包目录，只复制 Store host、词典、GPL、词典许可/署名、原应用图标的标准尺寸导出文件以及可选的实际政策和运行配置。`MakeAppx pack` 保留完整语义检查，不使用 `/nv`。打包后还检查没有 TSF DLL、注册程序、私有 registry hive 或本地签名，并且只有一个 Store host EXE。

GitHub CI 在 EXE 编译、测试、实际注册激活、截图和卸载之后构建开发身份 MSIX。Store 真实身份、授权和审批不会由开发配置冒充。分发版本的完整对应源码归档应包括两种实现和构建工具，保留词典许可。

Store listing 应说明 Windows 11 24H2+、x64、数字小键盘、真实启用方式和本地处理输入，不写开发过程。Store 截图来自实际运行的 Store host，不能用 EXE 的 TSF 注册界面冒充 MSIX 功能。GitHub 文档可另行链接支持 Windows 10 1809+ 的传统 EXE。

## 官方依据

- [MSIX 签名指南](https://learn.microsoft.com/en-us/windows/msix/package/sign-msix-package-guide)：Store 分发不要求开发者自己签名。
- [分发路线](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/choose-distribution-path)：Store MSIX 免费签名与更新；EXE/MSI 是独立路线。
- [能力声明](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations)：TextInputMethod 自定义能力和 Store 审批。
- [MakeAppx](https://learn.microsoft.com/en-us/windows/msix/package/create-app-package-with-makeappx-tool)：SDK 语义验证。
- [上传包格式](https://learn.microsoft.com/en-us/windows/msix/package/packaging-uwp-apps)：支持直接上传应用包，推荐 `.msixupload`。
