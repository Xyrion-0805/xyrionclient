# OpenCut 参考范围

2026-10-30 调查用户指定的 [OpenCut](https://github.com/OpenCut-app/OpenCut)，固定提交 `e668010778568641babef2cc40be4703ae6916d6`。其 README 说明主仓库正在重写，当前可用实现为 [OpenCut Classic](https://github.com/opencut-app/opencut-classic)，本次参考其提交 `cf5e79e919144200294fb9fed22a222592a0aeea`。

阅读的实现包括 Classic 的时间轴工具栏 `apps/web/src/timeline/components/timeline-toolbar.tsx`、分割操作 `apps/web/src/commands/timeline/element/split-elements.ts`、撤销命令接口 `apps/web/src/commands/base-command.ts`、导出面板 `apps/web/src/components/editor/export-button.tsx`。

采用的工作流：源素材和成片时间分离、非破坏式裁剪、多片段编排、分割、撤销重做、时间轴缩放与吸附、工程保存及明确的导出状态。实现为客户端原生 C++，使用现有 Qm 控件和 DDNet Demo / 视频引擎，未复制其 TypeScript 源码、图标或品牌资产。这不是完整 OpenCut 移植。

OpenCut 主仓库与 Classic 分别声明 MIT 许可，版权所有者分别为 Copyright (c) 2026 OpenCut、Copyright (c) 2025-2026 OpenCut。未将参考仓库放入发布包。若以后直接引入其源码或资产，须同时保留对应原文许可。

本项目对应实现：`src/engine/shared/demo_project.*`、`src/engine/client/demo_project_export.*`、`src/game/client/components/demo_timeline.*`。操作与输出说明见 [DEMO_EDITOR.md](DEMO_EDITOR.md)。
