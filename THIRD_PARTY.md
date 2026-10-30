# 来源、致谢与第三方许可

XyrionClient 是基于上游源码的修改版本，不代表上游官方发布。本项目新增的原创代码采用仓库 [LICENSE](LICENSE) 中的 MIT 许可；第三方代码、修改的上游文件与资源继续保留各自版权、许可和源码头注释。根目录 [license.txt](license.txt) 保留 Teeworlds / DDRace / DDNet 许可及资产说明，不同字体、资源和第三方库按各自许可分发。

| 来源 | 本项目使用范围 | 固定来源或许可位置 |
| --- | --- | --- |
| [DDNet Envelope Trigger PR #11398](https://github.com/ddnet/ddnet/pull/11398) | 触发定义、Switch 砖块图集、编辑器设置与补全；共用时钟为本工程适配 | `da9319bb4c55144d101f06067a4eae2e437b4ae0`；[联动说明](docs/xyrion/ENVELOPE_QQUAD.md)、[license.txt](license.txt) |
| [shengyan 北京服务端](https://github.com/shengyan42/shengyan-Beijing-server/) | 参考 QQuad 图层、动画与角色交互规则，适配到现有空间索引碰撞 | `6f6db8c9540189e6f7ed8220ec85a4a133d9d2d3`；[联动说明](docs/xyrion/ENVELOPE_QQUAD.md)、[来源许可](docs/xyrion/LICENSE-BeijingServer.txt) |
| [TClient](https://github.com/TaterClient/TClient) / [DDNet](https://github.com/ddnet/ddnet) | 客户端基础、网络、渲染、编辑器及工具 | 本仓库初始源码归档提交；[上游介绍](docs/upstream/TCLIENT_README.md)、[license.txt](license.txt) |
| [QmClient](https://github.com/wxj881027/QmClient) | 公共 UI、动画、控制台、输入法、弹道、Steam、分身视图、HUD 灵动岛、歌词解析及 Gores 统计等移植 | `2b626303c0985baf70c9fb599b26a4f35b8ac8c5`；[移植进度](docs/xyrion/QMCLIENT_PORT.md)、[来源许可](docs/xyrion/LICENSE-QmClient.txt) |
| [BestClient](https://github.com/BestProjectTeam/BestClient) | 跑图预测、共用媒体解码、聊天气泡、动画、地图预览及输入显示适配 | `b9beec12b7f18815d0aca03a2d54ccef0ad271ca`；[来源说明](docs/xyrion/BESTCLIENT_SOURCE.md)、[来源许可](docs/xyrion/LICENSE-BestClient.txt) |
| [ddnet-libs](https://github.com/ddnet/ddnet-libs) | 上游预编译依赖 | Git 子模块 `c0e6703fbcdbe03df2f26875427ec3951ce4ec21`，各库保留各自许可 |
| [Phosphor](https://phosphoricons.com/) | 四套图标字体 | [字体许可](data/fonts/Phosphor/LICENSE.txt) |
| [OpenCut](https://github.com/OpenCut-app/OpenCut) / [Classic](https://github.com/opencut-app/opencut-classic) | Demo 剪辑工作流参考，原生 C++ 实现 | MIT；固定提交及参考范围见 [OPENCUT_REFERENCE.md](docs/xyrion/OPENCUT_REFERENCE.md)，未复制 TS 源码或资产 |
| [Microsoft WebView2](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.2903.40) | Windows 内置浏览器 | SDK `1.0.2903.40`，SDK 内置许可；运行时另行安装 |
| [BtbN FFmpeg Builds](https://github.com/BtbN/FFmpeg-Builds) | 菜单视频解码运行库 | `ffmpeg-n7.1.1-56-gc2184b65d2-win64-gpl-shared-7.1`，2025-07-31 构建；运行包附 `LICENSE-FFMPEG.txt`，来源与构建配置在下载包中 |
| [yzrt 液态玻璃示例](https://gitee.com/greyd097/yzrt) | 部分玻璃表面的视觉参考 | 本地 GLSL 重做，非直接运行 CSS；[对应说明](docs/xyrion/UI_GLASS_SOURCE.md) |

NMClient 的部分可观察动画曾作为视觉参考，未将其可执行文件或安装包放入此仓库。QmClient 原版 HUD 灵动岛已替换旧 BC 组件；Windows MinGW 使用本项目 SMTC 桥接，音乐应用私有 Hook 与原生后端仍未移植。

`src/engine/external`、字体、皮肤及其他资产的许可见对应目录和根许可说明。分发客户端时应同时保留运行依赖的许可文件；不要将来源说明解释为整个仓库只有一种许可证。
