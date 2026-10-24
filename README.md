# XyrionClient

基于 [TClient](https://github.com/TaterClient/TClient) / [DDNet](https://github.com/ddnet/ddnet) 的 DDRace 客户端，包含 QmClient、BestClient 的部分移植与本项目的适配和功能扩展。

当前版本：**1.2.0，移植预览版**。主要构建目标为 Windows x64 / OpenGL。QmClient 的全部页面和媒体功能尚未完成移植，详见[移植进度](docs/xyrion/QMCLIENT_PORT.md)。

[功能说明](docs/xyrion/FEATURES.md) · [构建说明](docs/xyrion/BUILDING.md) · [更新记录](CHANGELOG.md) · [来源与许可](THIRD_PARTY.md) · [问题反馈](https://github.com/Xyrion-0805/xyrionclient/issues)

## 已接入的功能

- **界面和输入**：Qm 公共控件、动画、控制台设置、聊天命令补全和 Windows 输入法候选栏；Mod 页使用中性卡片和圆角控件。
- **三个分身**：主号加三个分身，独立连接、快照与输入；支持切换控制、单独断开及复制动作。
- **游戏辅助显示**：武器弹道、跑图预测、移动 quads 碰撞预测、冻结队友钩救站位与角度提示。
- **队伍统计**：Gores 落水榜仅在非 0 队伍中记录同队成员。
- **浏览器和背景**：WebView2 内置浏览器、浏览器顶部游戏控制开关、MP4/H.264 菜单背景。
- **媒体**：已有灵动岛、Windows 媒体控制、封面与歌词组件；Qm 原版媒体后端仍在适配中。
- **Mod 和聊天**：可视化积木、条件、变量、循环；可选自动翻译与提及触发的 AI 回复。

常用开关在“设置 → Xyrion”；Qm 功能有独立分组。具体默认值、使用方式和限制以[功能说明](docs/xyrion/FEATURES.md)为准。

## 运行与下载

当前仓库首先提供源码。发行包若已发布，可在 [Releases](https://github.com/Xyrion-0805/xyrionclient/releases) 获取；仓库中的源码 ZIP 不是可运行客户端。

解压完整运行包后启动 `XyrionClient.exe`，保留同目录 DLL、`data` 和 `storage.cfg`。内置浏览器需要安装 Microsoft Edge WebView2 Runtime。客户端沿用 `%APPDATA%\DDNet` 用户配置，不随源码上传个人配置、密钥和游戏数据。

## 获取与构建

```powershell
git clone --recurse-submodules https://github.com/Xyrion-0805/xyrionclient.git
cd xyrionclient
# 安装 Windows GNU 工具链、CMake、Ninja、Python 和 Rust GNU 后：
powershell -ExecutionPolicy Bypass -File scripts/build_xyrion.ps1 -Action Package
```

脚本优先使用项目旁 `.build-tools` 中的工具，也支持 PATH 和 `-ToolsDirectory`。详细环境、依赖版本及输出位置见[构建说明](docs/xyrion/BUILDING.md)。构建统一覆盖 `build-xyrion/XyrionClient.exe`，运行包位于 `dist/XyrionClient`。

仓库提供手动触发的 Windows 构建工作流，产物为 Actions artifact，不会自动创建 Release。上游多平台工作流已归档，本项目尚未宣称完成跨平台构建验收。

## 反馈与贡献

报告问题时请提供客户端版本、Windows 版本、图形后端、复现步骤和去除个人信息的日志。界面问题可附截图；涉及配置时请删除 API 密钥和个人账号数据。

贡献说明见 [CONTRIBUTING.md](CONTRIBUTING.md)。近期构建通过不等同于全部功能已完成游戏内验证，当前状态见[更新记录](CHANGELOG.md)。

## 致谢与许可

本项目是修改版本，不代表上游官方发布。感谢 Teeworlds、DDRace、DDNet、TClient、QmClient、BestClient 及其贡献者。新增原创代码采用 [MIT 许可](LICENSE)，第三方部分保留各自许可，具体组件及固定来源见 [THIRD_PARTY.md](THIRD_PARTY.md) 和 [license.txt](license.txt)。

上游 TClient 的完整项目介绍保留在 [docs/upstream/TCLIENT_README.md](docs/upstream/TCLIENT_README.md)。
