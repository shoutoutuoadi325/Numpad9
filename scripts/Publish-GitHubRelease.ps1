[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
$plan = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $root 'artifacts/github-release-plan.json') | ConvertFrom-Json
if ($env:GITHUB_REPOSITORY -ne 'shoutoutuoadi325/Numpad9' -or $plan.repository -ne $env:GITHUB_REPOSITORY -or $plan.tag -ne $env:RELEASE_TAG -or $plan.commit -ne $env:GITHUB_SHA) { throw 'Publication must use this repository, validated tag and the exact tested workflow commit.' }
if ($env:GITHUB_EVENT_NAME -notin 'push', 'workflow_dispatch' -or
    ($env:GITHUB_EVENT_NAME -eq 'push' -and $env:GITHUB_REF -notlike 'refs/tags/v*') -or
    ($env:GITHUB_EVENT_NAME -eq 'workflow_dispatch' -and $env:GITHUB_REF -notin @('refs/heads/main', "refs/tags/$($plan.tag)"))) { throw 'Publication is allowed only for a version-tag push or explicit dispatch on main/the exact existing tag.' }
if ($plan.tag -notmatch '^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$' -or -not $env:GH_TOKEN) { throw 'A validated release tag and scoped publication token are required.' }
$assets = Join-Path $root 'artifacts/github-release'
if (@($plan.validatedAssets).Count -ne 3) { throw 'Exact installer, corresponding source and SHA256 assets must be validated first.' }
foreach ($asset in $plan.validatedAssets) {
    if ($asset.name -notin @("Numpad9-$($plan.version)-x64-UNSIGNED.exe", "Numpad9-$($plan.version)-source.zip", 'SHA256SUMS.txt') -or
        (Get-FileHash -LiteralPath (Join-Path $assets $asset.name) -Algorithm SHA256).Hash -ne $asset.sha256) { throw 'A finalized release asset is missing, unexpected or changed.' }
}
function Invoke-ReleaseApi([string]$Path, [string]$Method = 'Get', [object]$Body = $null, [switch]$AllowNotFound) {
    $arguments = @{ Uri = "https://api.github.com/repos/$($plan.repository)/$Path"; Method = $Method;
        Headers = @{ Authorization = "Bearer $env:GH_TOKEN"; Accept = 'application/vnd.github+json'; 'X-GitHub-Api-Version' = '2022-11-28' };
        SkipHttpErrorCheck = $true; StatusCodeVariable = 'status' }
    if ($Body) { $arguments.Body = $Body | ConvertTo-Json -Compress; $arguments.ContentType = 'application/json' }
    $response = Invoke-RestMethod @arguments
    if ($status -eq 404 -and $AllowNotFound) { return $null }
    if ($status -lt 200 -or $status -ge 300) { throw "GitHub release API request failed (HTTP $status)." }
    return $response
}
function Get-ReleaseTagCommit {
    $reference = Invoke-ReleaseApi "git/ref/tags/$($plan.tag)" -AllowNotFound
    if (-not $reference) { return $null }
    $target = $reference.object
    for ($depth = 0; $depth -lt 10 -and $target.type -eq 'tag'; ++$depth) {
        $annotated = Invoke-ReleaseApi "git/tags/$($target.sha)"
        $target = $annotated.object
    }
    if ($target.type -ne 'commit') { throw 'Release tag does not resolve to a commit.' }
    return $target.sha
}
function Assert-DispatchHead {
    if ($env:GITHUB_REF -eq 'refs/heads/main') {
        $main = Invoke-ReleaseApi 'git/ref/heads/main'
        if ($main.object.sha -ne $plan.commit) { throw 'Remote main moved after the tested checkout; dispatch again for its current commit.' }
    }
}
Assert-DispatchHead
$existing = Invoke-ReleaseApi "releases/tags/$($plan.tag)" -AllowNotFound
if ($existing) { throw 'A release already exists for this tag. It is never overwritten.' }
$tagCommit = Get-ReleaseTagCommit
if ($tagCommit -and $tagCommit -ne $plan.commit) { throw 'Remote release tag points at an untested commit.' }
if (-not $tagCommit) {
    if (-not $plan.allowCreateTag -or $env:GITHUB_EVENT_NAME -ne 'workflow_dispatch' -or $env:GITHUB_REF -ne 'refs/heads/main') { throw 'This run is not authorized to create a missing release tag.' }
    # Only reached after exact installer testing and finalized asset hashes.
    $created = Invoke-ReleaseApi 'git/refs' -Method Post -Body @{ ref = "refs/tags/$($plan.tag)"; sha = $plan.commit }
    if ($created.object.sha -ne $plan.commit) { throw 'Created tag does not match tested commit.' }
}
$files = @($plan.validatedAssets | ForEach-Object { Join-Path $assets $_.name })
& gh release create $plan.tag @files --repo $plan.repository --verify-tag --draft --prerelease --title "Numpad9 $($plan.version) (unsigned)" --notes-file (Join-Path $root 'artifacts/github-release-notes.md')
if ($LASTEXITCODE -ne 0) { throw 'GitHub draft creation/upload failed; no publication requested.' }
$download = Join-Path ([IO.Path]::GetTempPath()) ('Numpad9-release-check-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $download | Out-Null
try {
    foreach ($asset in $plan.validatedAssets) {
        & gh release download $plan.tag --repo $plan.repository --pattern $asset.name --dir $download
        if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath (Join-Path $download $asset.name) -Algorithm SHA256).Hash -ne $asset.sha256) { throw 'Uploaded draft assets differ from the finalized files. Draft remains unpublished.' }
    }
    Assert-DispatchHead
    if ((Get-ReleaseTagCommit) -ne $plan.commit) { throw 'Release tag changed during asset upload. Draft remains unpublished.' }
    & gh release edit $plan.tag --repo $plan.repository --draft=false --prerelease
    if ($LASTEXITCODE -ne 0) { throw 'GitHub prerelease publication failed.' }
    $published = & gh release view $plan.tag --repo $plan.repository --json url,isDraft,isPrerelease
    if ($LASTEXITCODE -ne 0) { throw 'Published release status could not be read.' }
    $receipt = $published | ConvertFrom-Json
    if ($receipt.isDraft -or -not $receipt.isPrerelease) { throw 'Release did not reach the expected public prerelease state.' }
    [ordered]@{ url = $receipt.url; tag = $plan.tag; commit = $plan.commit; isDraft = $false; isPrerelease = $true; unsigned = $true; assets = $plan.validatedAssets } |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'artifacts/github-release-result.json') -Encoding UTF8
    Write-Host "Public unsigned prerelease verified: $($receipt.url)"
} finally { Remove-Item -LiteralPath $download -Recurse -Force }
