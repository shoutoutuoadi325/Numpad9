# 在 Mac 上签 Windows 安装包

可以在 macOS 上给 Windows EXE/DLL 做 Authenticode 签名。`scripts/Sign-Mac.py` 使用 osslsigncode，支持供应商 PKCS#11 硬件令牌/HSM 密钥，也支持已经存在、签发方允许使用的 PKCS#12。是否能从 Mac 调用密钥，取决于证书供应商的 macOS 驱动、PKCS#11 模块或云签名客户端；拥有证书文件并不等于能调用受保护的私钥。参考 [osslsigncode 官方 README](https://github.com/mtrojnar/osslsigncode#usage)。

现代公有代码签名密钥通常受硬件或受托云服务保护。优先使用供应商明确支持的非导出密钥流程；这里不要求导出密钥，不自动购买证书或注册账号，也不将开发自签名证书当作 Store 证书。Windows 安装和 Store 发布要求仍然适用，详见 [Microsoft EXE/MSI 要求](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-package-requirements)。

## 签名工具和凭据

安装兼容的 osslsigncode/OpenSSL 和供应商模块，例如通过 Homebrew 安装工具。先查看你安装版本的 `osslsigncode --help`，确认支持 RFC3161、`-readpass`、所选 provider/engine 和 `-require-leaf-hash`；不支持时停止并升级合适版本，不能绕过检查。

密钥、PFX、PIN 文件保存在仓库外。脚本默认在终端隐藏询问密码/PIN，也可用 `--password-file` 读取属于当前用户、权限不超过 `0600` 的文件，或 `--stdin-password` 接受安全的标准输入。不要把密码写进命令参数、shell 历史、Git、CI 输出或聊天。供应商需要额外认证时遵循其文档，不把 OTP 种子或私钥交给仓库。

公有签名证书链（PEM，签名 leaf 在最前）、签名 CA trust bundle、时间戳 CA trust bundle和证书 leaf 的 SHA256 指纹必须来自独立核对的来源。不要通过把开发证书加入信任 bundle 来制造“验证通过”。Windows 发布检查还会核对 Microsoft Trusted Root Program 的根。

## 签 payload

Windows CI 编译后的原始文件下载到 `release/x64` 和 `release/x86`。此目录只存公开二进制，不存密钥。Mac helper 输出到不同目录，保留 x64/x86 子目录，防止同名 DLL 冲突。

PKCS#11 模式示例（占位项必须替换为供应商真实配置）：

```sh
python3 scripts/Sign-Mac.py \
  --pkcs11-module '/vendor/module.dylib' --provider '/vendor/pkcs11-provider.dylib' \
  --key-uri 'pkcs11:token=YOUR_TOKEN;object=YOUR_KEY;type=private' \
  --certificates '/private/public-signing-chain.pem' \
  --ca-bundle '/private/public-ca-roots.pem' --tsa-ca-bundle '/private/tsa-roots.pem' \
  --leaf-sha256 'REPLACE_WITH_64_HEX_DIGITS' --timestamp-url 'https://YOUR_CA_RFC3161_ENDPOINT' \
  --base-dir release --output-dir signed-payload \
  release/x64/Numpad9.exe release/x64/Numpad9Ime.dll release/x86/Numpad9Ime.dll
```

如果供应商只支持 engine，则使用 `--engine` 替换 `--provider`；需要匹配该版本的 OpenSSL 和模块。公有证书通过 `--certificates` 提供，私钥通过 PKCS#11 URI 选中，PIN 不嵌入 URI。

已经持有签发方允许使用的 PKCS#12 时，将 token 相关选项替换为：

```text
--pkcs12 /private/existing-code-signing.p12
```

这是兼容已有凭据的选项，不表示新签发的公有 CA 证书通常可以导出为 PFX。脚本不导出私钥。它请求 SHA256 和 RFC3161 时间戳，检查 intended leaf 指纹、Code Signing EKU、非自签名 leaf、签名与时间戳链，然后写出公开的 `mac-signing-report.json`。失败文件不晋升为最终输出，已有输出不会覆盖；若批处理中部分先前文件已成功，保留它们的报告/状态并用新目录重试其余工作。

参数依据见 [osslsigncode 官方手册](https://github.com/mtrojnar/osslsigncode/blob/master/osslsigncode.md)。本 helper 的 Mac 验证不代替 Windows 的 Authenticode 信任检查，也不表示 Microsoft 已认证。

## 还需要签安装器内部的卸载程序

只签最外层安装 EXE 会漏掉 Inno 生成的 `unins000.exe`。必须先给 **Inno 的精确卸载程序缓存**签名，再把它嵌入安装器，最后签外层 EXE。Inno 的说明见 [SignedUninstaller](https://jrsoftware.org/ishelp/topic_setup_signeduninstaller.htm)。

此项目的 external 模式保留 `SignedUninstaller=yes`，使用已签名缓存，不在 Windows CI 放私钥。完整流程：

1. 冻结版本、原生构建、Inno 版本、图标和发行人元数据。用 `Build-Installer.ps1 -UnsignedDevelopment` 搭配真实 `-Publisher`、`-Contact`、`-SourceUrl` 准备安装元数据；此时的测试 EXE 不用于提交。
2. 在 Mac 签上述三个 payload，然后把已签名文件放回 Windows staging 的相同 x64/x86 路径，保留词典与许可证。
3. 首次生成卸载程序缓存时，在 Windows 本地或可交互构建环境直接调用 Inno，提供相同元数据和 `/DExternalSigning=1`：

```powershell
& '<ISCC.exe>' /DExternalSigning=1 /DAppVersion=0.1.0 `
    '/DPublisher=<真实证书发行人>' '/DContact=<真实联系信息>' `
    '/DSourceUrl=<对应源码 HTTPS 地址>' packaging/Numpad9.iss
```

Inno 在 `artifacts/signed-uninstaller` 中生成唯一名称的未签名缓存并要求签名。第一次编译可能停在此步骤；不能把该次输出当成完成。保留**原始缓存文件名及字节**，下载到 Mac，用 `Sign-Mac.py` 对它签名。缓存扩展名可能是 `.e32`；helper 按 PE 内容识别，不限定 `.exe`。以相同原始名称将签名后的缓存放回同一个 cache 目录；不要把一个已安装的 `unins000.exe` 改名充当缓存。

4. Windows 上重新编译：

```powershell
./scripts/Build-Installer.ps1 -ExternalSigning `
    -SignedUninstallerDirectory '<精确已签名缓存目录>' `
    -Publisher '<真实证书发行人>' -Contact '<真实联系信息>' `
    -SourceUrl '<对应源码 HTTPS 地址>'
```

生成 `Numpad9-0.1.0-x64-EXTERNAL-SIGNING-PENDING.exe`。其中卸载程序应已签名，外层 EXE 仍待签名。若 Inno 生成新的缓存文件，表示 compiler/version/icon/元数据影响了卸载程序内容：必须重新签新的缓存，不能跳过签名或重用不同内容的缓存。该跨机器流程的首次缓存生成需要配合 Inno 提示，尚未自动化成无人值守任务。

5. 将外层 EXE 下载到 Mac，再用相同 helper 签到新目录。可把签名后文件重命名为版本化候选名；重命名不改变内容。上传到最终不可变 HTTPS 地址后，在 Windows 运行 `Validate-Release.ps1`，自动核对所有 payload、安装器、实际安装出的卸载程序、发行人、版本、下载哈希、隐私及源码地址。人工实机 QA 不作为本项目的强制门槛。

Windows CI 之间可以传递已签 payload、已签卸载缓存和公开证书；不需要把私钥、PFX、PIN 或云服务登录凭据上传到仓库。CI 输出都保留候选/测试标识，直到外层签名和 Windows 发布检查完成。
