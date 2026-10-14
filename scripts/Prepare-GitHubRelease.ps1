[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Tag,
    [ValidateSet('Prepare', 'Finalize')][string]$Phase = 'Prepare',
    [switch]$AllowCreateTag,
    [string]$Repository = $env:GITHUB_REPOSITORY
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if ($Repository -ne 'shoutoutuoadi325/Numpad9') { throw 'This publisher/release configuration is for shoutoutuoadi325/Numpad9 only.' }
if ($Tag -notmatch '^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw 'Release tag must be v<major>.<minor>.<patch> without suffixes or leading zeroes.' }
$version = $Tag.Substring(1)
$project = Get-Content -Raw -LiteralPath (Join-Path $root 'CMakeLists.txt')
$sourceVersion = [regex]::Match($project, 'project\(Numpad9\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\b').Groups[1].Value
if ($sourceVersion -ne $version) { throw 'Release tag and source project version differ.' }
$headCommit = & git -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve checkout commit.' }
if ($env:GITHUB_SHA -and $env:GITHUB_SHA -ne $headCommit) { throw 'Checkout is different from the commit used by this workflow run.' }
$tagCommit = & git -C $root rev-parse --verify "${Tag}^{commit}" 2>$null
$createTag = $LASTEXITCODE -ne 0
if ($createTag) {
    if (-not $AllowCreateTag -or $env:GITHUB_EVENT_NAME -ne 'workflow_dispatch' -or $env:GITHUB_REF -ne 'refs/heads/main') { throw 'A missing release tag is allowed only by explicit workflow_dispatch on main with AllowCreateTag.' }
} elseif ($tagCommit -ne $headCommit) { throw 'Existing release tag points at a different commit than this checkout.' }
$changes = & git -C $root status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $changes) { throw 'Tracked source must be clean before release packaging.' }
$support = "https://github.com/$Repository/issues"
$sourceUrl = "https://github.com/$Repository/releases/download/$Tag/Numpad9-$version-source.zip"
$planPath = Join-Path $root 'artifacts/github-release-plan.json'
New-Item -ItemType Directory -Force -Path (Split-Path $planPath -Parent) | Out-Null
$plan = [ordered]@{ version = $version; tag = $Tag; commit = $headCommit; repository = $Repository; publisher = 'Zhiqi Zhang'; supportUrl = $support; sourceUrl = $sourceUrl; signing = 'unsigned'; prerelease = $true; allowCreateTag = $createTag; validatedAssets = $null }
$plan | ConvertTo-Json | Set-Content -LiteralPath $planPath -Encoding UTF8
if ($Phase -eq 'Prepare') {
    if ($env:GITHUB_OUTPUT) {
        @("version=$version", "source_url=$sourceUrl", "support_url=$support", 'publisher=Zhiqi Zhang') | Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding UTF8
    }
    Write-Host "Validated GitHub prerelease $Tag at commit $headCommit."
    exit
}
if ($env:OS -ne 'Windows_NT') { throw 'Final artifact checks require Windows.' }
$installer = Join-Path $root "artifacts/Numpad9-$version-x64-UNSIGNED.exe"
$installerItem = Get-Item -LiteralPath $installer
if ((Get-AuthenticodeSignature -LiteralPath $installer).Status -ne 'NotSigned') { throw 'The GitHub unsigned route requires an actually unsigned installer.' }
$versionInfo = $installerItem.VersionInfo
$binaryProductVersion = '{0}.{1}.{2}.{3}' -f $versionInfo.ProductMajorPart, $versionInfo.ProductMinorPart, $versionInfo.ProductBuildPart, $versionInfo.ProductPrivatePart
if ($versionInfo.CompanyName -cne 'Zhiqi Zhang' -or $versionInfo.ProductVersion -cne $version -or $binaryProductVersion -cne "$version.0") {
    throw "Public installer metadata differs from the release plan. CompanyName='$($versionInfo.CompanyName)'; ProductVersion='$($versionInfo.ProductVersion)'; binary ProductVersion='$binaryProductVersion'. Expected CompanyName='Zhiqi Zhang'; textual ProductVersion='$version'; binary ProductVersion='$version.0'."
}
$installerHash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
$smoke = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $root 'artifacts/installer-smoke/result.json') | ConvertFrom-Json
if (-not $smoke.passed -or $smoke.version -ne $version -or $smoke.installerSha256 -ne $installerHash -or -not $smoke.originalUserLanguagePreferencesRestored) { throw 'The exact public unsigned installer must pass the automated install/TSF/window/uninstall smoke with language preferences restored.' }
& (Join-Path $PSScriptRoot 'Build-SourceArchive.ps1') -Version $version
$source = Join-Path $root "artifacts/Numpad9-$version-source.zip"
$assets = Join-Path $root 'artifacts/github-release'
if (Test-Path -LiteralPath $assets) { Remove-Item -LiteralPath $assets -Recurse -Force }
New-Item -ItemType Directory -Force -Path $assets | Out-Null
Copy-Item -LiteralPath $installer, $source -Destination $assets
$sums = foreach ($file in Get-ChildItem -LiteralPath $assets -File | Sort-Object Name) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $file.Name
}
$sums | Set-Content -LiteralPath (Join-Path $assets 'SHA256SUMS.txt') -Encoding UTF8
$plan.validatedAssets = @(Get-ChildItem -LiteralPath $assets -File | Sort-Object Name | ForEach-Object { [ordered]@{ name = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash } })
$plan | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $planPath -Encoding UTF8
$notes = @"
Numpad9 $version · 手机布局的小键盘九键输入法

支持 Windows 10 1809+ / Windows 11 x64，包含用于 32 位应用的 x86 输入组件。离线中文拼音与英文候选，Num Lock 开关均按物理位置转换。

下载 **Numpad9-$version-x64-UNSIGNED.exe** 安装。此 EXE 未签名，Windows 可能显示未知发布者或信任提示。安装需要管理员授权；先在 Windows 语言设置添加「中文（简体，中国）」，再通过应用或 Win+Space 切换。密码和受保护的输入框不转换。

该安装器已通过自动安装、x64/x86 TSF 注册激活、主窗口和卸载检查。完整对应源码及构建工具在 **Numpad9-$version-source.zip**；两个文件的 SHA256 位于 **SHA256SUMS.txt**。应用 GPL-3.0，词典署名和许可随包及源码提供。

[使用说明](https://github.com/$Repository/tree/$Tag#readme) · [问题反馈]($support) · [Microsoft Store](https://apps.microsoft.com/detail/9N82M2S3QS4C)

Microsoft Store 版本尚未发布；以上 Store 链接在商店发布后用于安装。

Unsigned GitHub prerelease for Windows x64. Includes offline Chinese Pinyin and English input, x86 text-service support, full corresponding source and SHA256 checksums. Windows may show an unknown-publisher warning. Administrator approval and the Chinese (Simplified, China) user language are required. No commercial certificate is needed for this GitHub distribution.
"@
$notes | Set-Content -LiteralPath (Join-Path $root 'artifacts/github-release-notes.md') -Encoding UTF8
Write-Host "Exact unsigned prerelease assets prepared in $assets."
