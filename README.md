# XyrionClient

基于 [TClient](https://github.com/TaterClient/TClient) / [DDNet](https://github.com/ddnet/ddnet) 的 DDRace 客户端，包含 QmClient、BestClient 的部分移植与本项目的适配和功能扩展。

当前版本：**1.2.0，移植预览版**。主要构建目标为 Windows x64 / OpenGL。QmClient 的全部页面和媒体功能尚未完成移植，详见[移植进度](docs/xyrion/QMCLIENT_PORT.md)。

[功能说明](docs/xyrion/FEATURES.md) · [构建说明](docs/xyrion/BUILDING.md) · [更新记录](CHANGELOG.md) · [来源与许可](THIRD_PARTY.md) · [问题反馈](https://github.com/Xyrion-0805/xyrionclient/issues)

## 已接入的功能

- **界面和输入**：Qm 公共控件、动画、控制台设置、聊天命令补全和 Windows 输入法候选栏；Mod 页使用中性卡片和圆角控件。
- **三个分身**：主号加三个分身，独立连接、快照与输入；支持切换控制、单独断开。
- **游戏辅助显示**：武器弹道、跑图进度、移动方块移动水的延迟补偿、冻结队友钩救站位与角度提示。
- **队伍统计**：Gores 落水次数显示在 Tab 名字前，仅记录非 0 队伍的同队成员，本队 teamkill 后重新统计。
- **浏览器和背景**：WebView2 内置浏览器、Alt 切换浏览器与游戏操控、MP4/H.264 菜单背景。
- **独立声音开关**：声音页可分别启停 41 类内置声音，设置自动保存。
- **媒体**：已用 Qm 原版 HUD 灵动岛替换 BC，包含液态动画、封面切换、歌词和计分板媒体控制；Windows 使用后台 SMTC 桥接，音乐应用专用 Hook 仍在适配中。
- **Mod 和聊天**：可视化积木；可选自动翻译与提及触发的 AI 回复。
- **编辑器与服务器**：Quad Slice 矩形裁剪；内附 Xyrion 修改版 `DDNet-Server.exe`，支持 Envelope Trigger 触发 QQuad 的单次、循环、暂停及继续播放，服务端碰撞与客户端动画同步。配置见 [Envelope Trigger 与 QQuad](docs/xyrion/ENVELOPE_QQUAD.md)。

常用开关在“设置 → Xyrion”；Qm 功能有独立分组。具体默认值、使用方式和限制以[功能说明](docs/xyrion/FEATURES.md)为准。

## 运行与下载

当前仓库首先提供源码。发行包若已发布，可在 [Releases](https://github.com/Xyrion-0805/xyrionclient/releases) 获取；仓库中的源码 ZIP 不是可运行客户端。

解压完整运行包后启动 `XyrionClient.exe`，保留同目录 DLL、`data` 和 `storage.cfg`。内置浏览器需要安装 Microsoft Edge WebView2 Runtime。客户端沿用 `%APPDATA%\DDNet` 用户配置，不随源码上传个人配置、密钥和游戏数据。

运行包同时包含服务端。在解压目录启动 `DDNet-Server.exe`，可用随附 `autoexec_server.cfg` 配置地图和服务器参数。默认不公开注册；要使用包络触发与 QQuad 物理联动，需要运行这个修改版服务端。配置与示例见 [联动说明](docs/xyrion/ENVELOPE_QQUAD.md)。

## 获取与构建

Demo 播放器新增“剪辑工作台”，支持多片段裁剪、排序、分割、变速、撤销重做、工程保存，以及 Demo 片段和 MP4 输出。用法见 [Demo 剪辑说明](docs/xyrion/DEMO_EDITOR.md)，参考来源见 [OpenCut](docs/xyrion/OPENCUT_REFERENCE.md)。

```powershell
git clone --recurse-submodules https://github.com/Xyrion-0805/xyrionclient.git
cd xyrionclient
# 安装 Windows GNU 工具链、CMake、Ninja、Python 和 Rust GNU 后：
powershell -ExecutionPolicy Bypass -File scripts/build_xyrion.ps1 -Action Package
```

脚本优先使用项目旁 `.build-tools` 中的工具，也支持 PATH 和 `-ToolsDirectory`。详细环境、依赖版本及输出位置见[构建说明](docs/xyrion/BUILDING.md)。构建统一覆盖 `build-xyrion` 内的客户端与服务端，运行包位于 `dist/XyrionClient`，同时包含 `XyrionClient.exe` 和 `DDNet-Server.exe`。

仓库提供手动触发的 Windows 构建工作流，产物为 Actions artifact，不会自动创建 Release。上游多平台工作流已归档，本项目尚未宣称完成跨平台构建验收。

## 反馈与贡献

报告问题时请提供客户端版本、Windows 版本、图形后端、复现步骤和去除个人信息的日志。界面问题可附截图；涉及配置时请删除 API 密钥和个人账号数据。

贡献说明见 [CONTRIBUTING.md](CONTRIBUTING.md)。近期构建通过不等同于全部功能已完成游戏内验证，当前状态见[更新记录](CHANGELOG.md)。

## 致谢与许可

本项目是修改版本，不代表上游官方发布。感谢 Teeworlds、DDRace、DDNet、TClient、QmClient、BestClient 及其贡献者。新增原创代码采用 [MIT 许可](LICENSE)，第三方部分保留各自许可，具体组件及固定来源见 [THIRD_PARTY.md](THIRD_PARTY.md) 和 [license.txt](license.txt)。

上游 TClient 的完整项目介绍保留在 [docs/upstream/TCLIENT_README.md](docs/upstream/TCLIENT_README.md)。
