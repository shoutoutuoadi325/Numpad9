# 可选的 EXE 代码签名

GitHub 当前提供明确标注的未签名 EXE。持有可信代码签名凭据时，可以给 EXE、DLL 和安装器增加 Authenticode 签名。Microsoft Store 的 [MSIX 路线](store/msix.md) 由 Store 签名，独立于这里的 EXE 流程。

## Windows 签名

`scripts/Sign-File.ps1` 使用 Windows SDK 的 SignTool，以及当前用户 `My` 证书存储中的精确证书指纹：

```powershell
$env:NUMPAD9_SIGNTOOL = '<signtool.exe 路径>'
$env:NUMPAD9_CERT_THUMBPRINT = '<证书 SHA1 指纹>'
$env:NUMPAD9_TIMESTAMP_URL = '<供应商 RFC3161 时间戳网址>'
./scripts/Build-Installer.ps1 -Publisher '<发行人>' -Contact '<支持联系方式>' `
    -SourceUrl '<版本固定的完整源码 HTTPS 地址>'
```

此模式签名载荷，并由 Inno Setup 签名生成的卸载程序和外层安装器。结果是 `SIGNED-CANDIDATE.exe`。源码与词库许可仍需随发行提供。

## macOS 签名

`scripts/Sign-Mac.py` 使用 osslsigncode/OpenSSL。供应商须支持 macOS 的 PKCS#11 模块、provider/engine 或已有且允许使用的 PKCS#12 容器。凭据保存在仓库外，PIN/密码由隐藏提示或受保护的 `0600` 文件读取，不放入命令参数、Git 或日志。

```sh
python3 scripts/Sign-Mac.py \
  --pkcs11-module '/vendor/module.dylib' --provider '/vendor/provider.dylib' \
  --key-uri 'pkcs11:token=TOKEN;object=KEY;type=private' \
  --certificates '/private/public-chain.pem' \
  --ca-bundle '/private/ca-roots.pem' --tsa-ca-bundle '/private/tsa-roots.pem' \
  --leaf-sha256 '<证书 DER SHA256，64 位十六进制>' \
  --timestamp-url '<供应商 RFC3161 时间戳网址>' \
  --base-dir release --output-dir signed-payload \
  release/x64/Numpad9.exe release/x64/Numpad9Ime.dll release/x86/Numpad9Ime.dll
```

公有证书链中 signing leaf 在最前；证书指纹和签名/时间戳 CA bundle 应来自独立可信来源。供应商使用 engine 时以 `--engine` 替换 `--provider`；使用已有 PKCS#12 时以 `--pkcs12` 替换 token 选项。完整参数见 `python3 scripts/Sign-Mac.py --help`。

## Inno 卸载程序与外部签名

仅签外层 EXE 不会签名内嵌载荷或 `unins000.exe`。外部签名按以下顺序处理：

1. 固定产品版本、发行元数据、Inno 编译器和图标，用 `Build-Installer.ps1 -UnsignedDevelopment` 提供真实的 `-Publisher`、`-Contact`、`-SourceUrl` 生成元数据，然后签名 `release/x64` 和 `release/x86` 中的 PE 载荷。
2. 将已签名载荷放回原目录，首次调用 Inno 生成精确卸载缓存：

```powershell
& '<ISCC.exe>' /DExternalSigning=1 /DAppVersion=0.1.0 `
    '/DPublisher=<发行人>' '/DContact=<支持联系方式>' `
    '/DSourceUrl=<对应源码 HTTPS 地址>' packaging/Numpad9.iss
```

3. Inno 在 `artifacts/signed-uninstaller` 生成缓存并要求签名。保留其原始名称和内容，用 `Sign-Mac.py` 签名后放回同一缓存目录；helper 接受 `.e32` 等 PE 缓存文件。不要以已安装的 `unins000.exe` 替代缓存。
4. 在 Windows 重新构建，再签名外层安装器：

```powershell
./scripts/Build-Installer.ps1 -ExternalSigning `
    -SignedUninstallerDirectory '<精确已签名缓存目录>' `
    -Publisher '<发行人>' -Contact '<支持联系方式>' `
    -SourceUrl '<对应源码 HTTPS 地址>'
```

输出 `EXTERNAL-SIGNING-PENDING.exe`，外层由 Mac helper 签到新目录。若 Inno 生成新缓存，应签名新的缓存再构建。最终签名载荷、安装器和卸载程序的结构检查由 `scripts/Validate-Release.ps1` 提供。

官方工具说明：[osslsigncode](https://github.com/mtrojnar/osslsigncode#usage)、[Inno SignedUninstaller](https://jrsoftware.org/ishelp/topic_setup_signeduninstaller.htm)。
