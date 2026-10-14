# 构建与项目结构

## Windows EXE

需要 Visual Studio 2022 的 C++ 桌面开发组件、Windows SDK、CMake 3.20+、PowerShell 7，以及 Inno Setup 6.3+。

```powershell
./scripts/Build-Windows.ps1
./scripts/Build-Installer.ps1 -UnsignedDevelopment
```

原生载荷输出到 `release/x64` 和 `release/x86`，安装器为 `artifacts/Numpad9-0.1.0-x64-UNSIGNED-TESTING.exe`。公开发行的元数据和文件名由 [GitHub 发布流程](release-github.md) 提供。

安装器从 [EXE 隐私政策模板](privacy-exe.template.md) 填入发行人、支持联系方式和源码地址；模板中的占位符由 `Build-Installer.ps1` 替换。

## MSIX

Store host 需要 Windows SDK 10.0.26100.0，以及 Visual Studio 2022 或 2026 的 C++ 工具。构建及真实包身份参数见 [MSIX 打包](store/msix.md)。

## macOS / Linux 输入引擎

需要 C++17 编译器和 CMake 3.20+：

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/native --parallel
```

该命令构建独立输入引擎。Windows 主程序和输入服务使用 Windows 工具链构建；macOS 上的 EXE 签名见 [签名说明](signing.md)。

## 项目结构

| 目录 | 内容 |
| --- | --- |
| `src/core` | 两种发行共用的九键输入引擎 |
| `src/ime` | EXE 发行的 x64/x86 TSF 输入组件 |
| `src/msix` | 商店版 Windows 文本输入 API 接入 |
| `src/app` | 应用窗口、候选界面和资源 |
| `data` / `tools` | 词库、来源快照、许可与重建工具 |
| `packaging` / `scripts` / `.github` | 安装器、MSIX 身份、构建和发布流程 |
| `tests` | 核心、Windows 输入服务及安装生命周期检查代码 |

`build/`、`release/`、`artifacts/` 是生成目录。实际授权配置保存在忽略的 `private/` 中，不进入源码归档。生成对应源码包使用 `scripts/Build-SourceArchive.ps1`，词库重建见 [dictionary.md](dictionary.md)。
