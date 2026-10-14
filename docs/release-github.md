# GitHub EXE 发布

GitHub 分发传统 TSF EXE，发行人为 Zhiqi Zhang，支持渠道为 [GitHub Issues](https://github.com/shoutoutuoadi325/Numpad9/issues)。[v0.1.0](https://github.com/shoutoutuoadi325/Numpad9/releases/tag/v0.1.0) 是公开预发行；Microsoft Store 使用独立的 [MSIX 路线](store/msix.md)。

## 发布资产

- `Numpad9-<version>-x64-UNSIGNED.exe`：含 x64 主程序和 x64/x86 TSF DLL 的离线安装器。
- `Numpad9-<version>-source.zip`：与 tag 对应的完整源码、词典来源、许可及构建工具。
- `SHA256SUMS.txt`：以上两个文件的 SHA256。

未签名状态在文件名和发行说明中明确标注。`UNSIGNED-TESTING.exe` 是构建产物，不重命名为公开安装器；公开包使用真实发行元数据重新构建。可选签名见 [signing.md](signing.md)。

## 新版本发布

1. 同步 CMake 项目版本、Windows 版本资源和构建脚本的版本默认值，将代码提交到 `main`。
2. 打开 GitHub Actions 的 **Windows candidate build**，选择 `main`，将 `release_tag` 填为新的 `v<major>.<minor>.<patch>`。使用已有 tag 时，它必须指向同一个提交，且尚无同名 release。
3. 工作流构建原生载荷，以真实发行人、支持和源码地址生成公开 EXE，再生成源码包与校验文件。全部资产来自同一次运行的精确 checkout。
4. 发布 job 创建草稿，上传并核对资产，然后公开预发行。已有 release 不覆盖，已有 tag 不移动；若 `main` 在运行期间改变，需针对新提交重新发起。

工作流也支持 `v*` tag 推送；普通 `main` 推送和 PR 不发布 release。MSIX 构建 job 与 EXE 发布独立。运行入口为 `.github/workflows/windows.yml`，版本与资产处理分别由 `Prepare-GitHubRelease.ps1`、`Publish-GitHubRelease.ps1` 实现。

完整对应源码必须保留 GPL 和词库许可。LAF 运行配置及含授权配置的 MSIX 不上传到公开 GitHub Release。发行说明保留 [Microsoft Store 链接](https://apps.microsoft.com/detail/9N82M2S3QS4C)，下载可用性以商店页面为准。
