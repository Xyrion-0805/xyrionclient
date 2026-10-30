# Envelope Trigger 与 QQuad

当前 Xyrion 服务端与客户端共享包络触发时钟。地图加载／重载先应用初始播放方式；触发后，普通地图 Quad、实体视图、客户端预测碰撞和服务端权威碰撞使用相同的动画位置。

## 最小示例

在编辑器中给 QQuad 图层（例如 `QFr` 或 `QHook`）的 Quad 设置位置包络，然后在 **Server settings** 添加：

```text
envelope_trigger_spawn pause
envelope_trigger 1 reset_start_once 0
```

- `pause` 让包络在地图加载时停在起点。
- 在 **Switch** 层放置 **Envelope Trigger Team**（砖块 ID 177），`Number` 设为 `1`，`Delay` 设为 `0`。
- 玩家碰到砖块后，本队的第 `0` 号包络从头播放一次，结束后停在末帧。
- 包络索引从 **0** 开始。命令末尾是包络索引，首个数字是触发区编号，二者独立。
- 同一区可以配置多条 `envelope_trigger`，同时控制多个包络。
- 在暂停起点不需要重置的场合，也可以使用 `envelope_trigger 1 start_once 0`。

命令格式沿用 DDNet PR #11398：

```text
envelope_trigger <zone> <trigger_type> <envelope>
envelope_trigger_spawn <trigger_type>
tune_zone_envelope_trigger <tune_zone> <zone>
```

触发区编号为 `Number + Delay × 256`，范围为 0～65535；这里的 Delay 是编号高位，不是播放延迟。Team 砖块作用于 DDRace 队伍（0 队伍也共享）；Solo 砖块 ID 176 仅作用于触发者。停留在同一触发区不会每 tick 重启动画，离开后再进入可以再次触发。

## trigger_type

| 值 | 行为 |
| --- | --- |
| `default` | 恢复地图全局时间播放 |
| `pause` | 停在当前播放位置 |
| `start_once` | 从当前播放位置继续，播放一次后停在末帧 |
| `start_loop` | 从当前播放位置继续循环 |
| `stop` | 停止并回到起点 |
| `reset_start_once` | 从头播放一次，结束停在末帧 |
| `reset_start_loop` | 从头循环播放 |

支持位置包络（位移、旋转）与颜色包络；颜色 alpha 为零时相应碰撞 Quad 隐藏。Quad 原有 envelope offset 继续生效；需要停在动画原始起点时，将该 offset 设为 0。

## 队伍与同步

Team 动画由服务端保存，队友后来加入时取得本队当前播放位置。个人死亡清除个人 Solo 状态，不重启仍在运行的本队动画；队伍 switchers 重置时，本队动画一起回到地图初始状态。换图或重载清除触发记录。

快照同时包含触发类型、绝对起始 tick 和暂停位置，避免丢包或暂停／继续后错位。主号和各分身按自己的角色接收状态；观战时按被观战角色接收。客户端 Demo 保存这套快照，拖动播放位置后直接恢复对应状态；服务端 Demo 使用快照中记录的单一视角。

物理联动要求使用本次修改的 Xyrion 服务端。客户端识别权威包络快照时自动启用所需的 QQuad 碰撞；普通外部服务器仍沿用用户原有移动方块预测选项。这不是为未修改的北京服务端远程增加触发能力。

## 来源与本次交付

- DDNet PR [#11398](https://github.com/ddnet/ddnet/pull/11398)，提交 `da9319bb4c55144d101f06067a4eae2e437b4ae0`：触发参数、砖块编号、编辑器资源及补全。
- [北京服务端](https://github.com/shengyan42/shengyan-Beijing-server/)，`master` 提交 `6f6db8c9540189e6f7ed8220ec85a4a133d9d2d3`：参考 QQuad 图层、包络求值及角色交互规则；沿用本工程已有空间索引，未复制其逐帧重建网格。
- 共用播放状态与服务端权威同步为本工程适配实现；保留 PR 的 EnvelopeTrigger UUID，并追加有默认值的暂停位置字段。

按用户要求，本次只编译，不运行测试或游戏内实测。
