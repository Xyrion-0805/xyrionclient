# 液态玻璃参考与图层对应

参考：https://gitee.com/greyd097/yzrt/tree/master/纯CSS液态玻璃
参考样式：`纯CSS液态玻璃/css/styles.css`，Gitee blob `b462215df3de68b88ef400ee56d0fea103148382`。

`data/shader/ui_glass.frag` 在 OpenGL 中重新实现 `.box` 的图层组合。CSS 不能直接用于原生游戏；模糊、阴影与抗锯齿由 GLSL 采样及距离场近似，不能声称与浏览器逐像素相同。

| 示例参数 | 游戏中的实现 |
| --- | --- |
| backdrop blur(2px) | 九点 Gaussian 背景采样 |
| background white .04 | 模糊背景混合 4% 白色 |
| 1px double dark .08 | 深色细轮廓 |
| opposing inset 2px/-2px .9, 6px/-6px .55 | 两组相反方向内高光 |
| inset 0 0 2px black .8 | 黑色内轮廓 |
| 0 4px 8px black .2 | 向下 4px，sigma 4px 的外阴影 |
| :before 1px black .9, top35%, blur8px | 下移的模糊黑色内轮廓 |
| :after 45deg white .8, transparent25%-75% | 对角渐变高光 |
| circle-overlay white .2, inset4.5px, blur1px | 柔和的次级内高光 |
| brightness(.9) | 最后乘以 0.9 |

UI 的原有透明度用于展开／淡出动画，保留少量控件色调以识别选择状态。外阴影网格扩展 16px，避免截断。全局设置关闭时沿用原有 UI 绘制。背景每帧捕获一次，避免为每个控件复制整个画面。

按用户最新要求，白色高光另乘约 1px 边缘遮罩，强度降到细高光 24%／宽高光 10%／对角高光 12%，次级内高光移至边缘并降到 2.5%。不保留示例中大面积白色渐变羽化。新增 0%–100% 不透明度，默认 100%，在最终玻璃表面及外阴影合成时应用，文字不受影响。

最新不透明度调整：100% 作为默认，常规 UI 的原始 25% 背景不再把玻璃限制为 50% 合成。中性控件保留 48% 原背景色填充，有色控件提高到 90%，中间档位也使用更高的合成不透明度；0% 仍隐藏玻璃表面。边缘高光强度不增加。
