# XyrionClient 构建说明

当前实际发布目标为 Windows x64、GNU/MinGW 和 OpenGL。源码沿用上游 CMake/Rust 构建系统；其他平台及 Vulkan 暂未完成本项目新增功能的联合验收。

## 获取源码和依赖

```powershell
git clone --recurse-submodules https://github.com/Xyrion-0805/xyrionclient.git
cd xyrionclient
```

已有普通克隆可执行：

```powershell
git submodule update --init --recursive
```

`ddnet-libs` 以 Git 子模块固定到 `c0e6703fbcdbe03df2f26875427ec3951ce4ec21`，不把第三方依赖副本提交到主仓库。不要只下载 GitHub 自动生成的源码 ZIP 后直接编译，ZIP 不包含子模块内容。

## Windows 工具

需要以下工具位于 PATH，或放进项目旁的 `.build-tools`：

| 工具 | 要求或已使用版本 |
| --- | --- |
| GCC、G++、windres、ar | Windows x64 GNU；本地使用 w64devkit 2.10.0 |
| CMake | 3.31 或更新 |
| Ninja | 支持当前 CMake 的版本 |
| Python | Python 3 |
| Rust / Cargo | 至少 1.85，Windows GNU 工具链 |

如果安装的 Rust 默认是 MSVC，可在当前终端选择 GNU：

```powershell
rustup toolchain install stable-x86_64-pc-windows-gnu --profile minimal
$env:RUSTUP_TOOLCHAIN = 'stable-x86_64-pc-windows-gnu'
```

脚本优先查找 `.build-tools/w64devkit/bin`、`.build-tools/python-packages/cmake/data/bin`、`.build-tools/python-packages/bin` 与 `.build-tools/cargo/bin`，没有对应工具时回退 PATH。`-ToolsDirectory C:/path/to/tools` 可指定其他工具缓存目录；脚本不修改系统 PATH。使用已有本地工具时沿用其 Cargo/Rustup 缓存，使用系统工具时保留用户环境。

## 编译与打包

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_xyrion.ps1 -Action Package -Jobs 4
```

| 操作 | 内容 |
| --- | --- |
| `Configure` | 准备运行依赖并生成 CMake 配置 |
| `Build` | 增量编译并覆盖客户端与服务端，不运行测试 |
| `Package` | 编译客户端与服务端并整理完整运行目录，不运行测试 |
| `Test` | 构建并运行脚本指定的自动测试集合 |
| `All` | 配置、构建、测试、打包 |

默认操作为 `All`，只需要构建包时请明确写 `-Action Package`。只使用 `build-xyrion`，不创建 next/update 构建目录。若客户端占用输出文件，先关闭对应实例。

- 编译输出：`build-xyrion/XyrionClient.exe` 与 `build-xyrion/DDNet-Server.exe`。
- 完整运行目录：`dist/XyrionClient/`。
- 脚本不会自动生成 ZIP；可对整个运行目录压缩，不能只分发 exe。
- 不自动覆盖 Steam 安装目录，也不自动发布 GitHub Release。

运行目录内含两套可执行程序、共用 DLL／地图资源和 `autoexec_server.cfg`。启动 `DDNet-Server.exe` 即运行修改版服务端；Envelope Trigger 与 QQuad 联动的地图设置见 [ENVELOPE_QQUAD.md](ENVELOPE_QQUAD.md)。服务端二进制随 Release 压缩包分发，不提交到源码树。

## 运行依赖

- WebView2 SDK 固定为 `1.0.2903.40`，由 `setup_webview2.ps1` 下载，解压前校验 SHA-256；运行时另需 Edge WebView2 Runtime。
- FFmpeg 播放库使用固定的 BtbN 7.1.1 shared 包，由 `setup_playback.ps1` 下载并校验 SHA-256。`VIDEORECORDER=ON` 提供视频解码集成；脚本在编译结束或失败后恢复播放 DLL。
- STEAM 开启，Vulkan、Discord 和客户端自动更新关闭。运行时 DLL、字体、着色器、Steam 文件与许可会一并复制。
- `storage.cfg` 使用标准 DDNet 用户目录，Windows 上通常为 `%APPDATA%\DDNet`。密钥、cfg、录像、截图和本地 Mod 不纳入仓库。

FFmpeg 包及各依赖的许可应与分发包一起保留，来源索引见仓库根目录 `THIRD_PARTY.md`。

## GitHub Actions

`Windows package (manual)` 只通过 Actions 页面手动触发，准备固定 w64devkit、Python、CMake、Ninja、Rust GNU 和子模块，调用同一 `Package` 脚本并上传完整运行目录。它不运行游戏或发布 Release，也不构成完整功能验收。上游历史工作流位于 `docs/upstream/workflows`，不自动执行。

`scripts/verify_*.py` 是历史本地交互检查工具，可能依赖额外 Python 库和桌面环境；它们不属于 `Package`，也不能代替实际游戏场景验收。一次性源码移植脚本 `scripts/port_qmclient.py` 不属于构建流程，不要对已适配源码重复运行。
