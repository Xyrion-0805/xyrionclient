#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_MOD_BLOCKS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_MOD_BLOCKS_H

#include <array>
#include <string_view>

namespace modsystem
{
enum EHook { HOOK_ON_HOOK_FIRE, HOOK_ON_HOOK_ATTACH, HOOK_ON_JUMP, HOOK_ON_GROUND, HOOK_ON_FREEZE, HOOK_ON_UNFREEZE, HOOK_ON_DEATH, HOOK_ON_FINISH, HOOK_ON_SPAWN, HOOK_ON_CONNECT, HOOK_ON_TICK, HOOK_ON_KEY_PRESS, NUM_HOOKS };
enum EAction { ACT_SET_HOOK_ANGLE, ACT_SEND_CHAT, ACT_RUN_COMMAND, ACT_ECHO, ACT_PLAY_SOUND, ACT_SET_COLOR, ACT_GET_VALUE, ACT_STORE_VAR, ACT_LOAD_VAR, NUM_ACTIONS };
enum ECondition { COND_NONE, COND_HOOK_WILL_HIT_PLAYER, COND_HOOK_WILL_HIT_SOLID, COND_HOOK_WILL_HIT_IRON, COND_HOOK_IS_READY, COND_HOOK_AIM_LEFT, COND_HOOK_AIM_RIGHT, COND_HOOK_AIM_UP, COND_HOOK_AIM_DOWN, COND_IS_ON_GROUND, COND_IS_AIRBORNE, COND_RANDOM_PROBABILITY, COND_VAR_EQ, COND_VAR_LT, COND_VAR_GT, NUM_CONDITIONS };
enum ENearestTarget { TARGET_PLAYER, TARGET_SOLID, TARGET_IRON, TARGET_HOOKABLE, TARGET_ANY, NUM_TARGETS };
enum EGetValue { GETVAL_POS_X, GETVAL_POS_Y, GETVAL_VEL_X, GETVAL_VEL_Y, GETVAL_HOOK_ANGLE, GETVAL_NEAREST_X, GETVAL_NEAREST_Y, GETVAL_NEAREST_DISTANCE, GETVAL_NEAREST_ANGLE, GETVAL_NEAREST_DX, GETVAL_NEAREST_DY, GETVAL_NEAREST_VALID, NUM_GETVALUES };
enum EParamKind { PARAM_NONE, PARAM_INT, PARAM_STR, PARAM_VAR };
enum ELogic { LOGIC_AND, LOGIC_OR };

inline constexpr std::array<const char *, NUM_HOOKS> HOOK_KEYS = {"OnHookFire", "OnHookAttach", "OnJump", "OnGround", "OnFreeze", "OnUnfreeze", "OnDeath", "OnFinish", "OnSpawn", "OnConnect", "OnTick", "OnKeyPress"};
inline constexpr std::array<const char *, NUM_HOOKS> HOOK_NAMES = {"发射钩子时", "钩子附着时", "跳跃时", "落地时", "冻结时", "解冻时", "死亡时", "完成比赛时", "出生时", "连接时", "每个 tick", "按键时"};
inline constexpr std::array<const char *, NUM_ACTIONS> ACTION_KEYS = {"SetHookAngle", "SendChat", "RunCommand", "Echo", "PlaySound", "SetColor", "GetValue", "StoreVar", "LoadVar"};
inline constexpr std::array<const char *, NUM_ACTIONS> ACTION_NAMES = {"设置钩子角度", "发送聊天", "执行命令", "显示文字", "播放声音", "设置颜色", "获取数值", "存入变量", "读取变量"};
inline constexpr std::array<const char *, NUM_ACTIONS> ACTION_HELP = {"角度单位为度：0 向右，90 向下。支持 %v、变量和表达式。", "发送聊天文字；%变量名% 和 %v 会替换为当前值。", "执行客户端控制台命令，文字支持参数和变量占位符。", "在本地显示文字，支持 %变量名% 和 %v。", "播放声音编号，编号支持表达式。", "设置身体颜色（打包的 HSL 整数），支持表达式。", "把所选数值放入 %v；目标值仅取当前瞄准射线的命中结果。", "把 %v 存入具名变量，跨 tick 保留。", "把具名变量或数值参数读入 %v。"};
inline constexpr std::array<const char *, NUM_CONDITIONS> CONDITION_KEYS = {"None", "HookWillHitPlayer", "HookWillHitSolid", "HookWillHitIron", "HookIsReady", "HookAimLeft", "HookAimRight", "HookAimUp", "HookAimDown", "IsOnGround", "IsAirborne", "RandomProbability", "VarEq", "VarLt", "VarGt"};
inline constexpr std::array<const char *, NUM_CONDITIONS> CONDITION_NAMES = {"始终满足", "钩子将命中玩家", "钩子将命中砖块", "钩子将命中铁块", "钩子已就绪", "瞄准向左", "瞄准向右", "瞄准向上", "瞄准向下", "站在地面", "在空中", "随机概率（%）", "变量等于", "变量小于", "变量大于"};
inline constexpr std::array<const char *, NUM_TARGETS> TARGET_NAMES = {"玩家", "砖块", "铁块", "可钩目标", "任意目标"};
inline constexpr std::array<const char *, NUM_GETVALUES> VALUE_NAMES = {"自身 X", "自身 Y", "速度 X", "速度 Y", "当前钩子角度", "目标 X", "目标 Y", "目标距离", "目标相对角度", "目标相对 X", "目标相对 Y", "目标是否取到（1/0）"};

template<size_t N> int KeyIndex(const std::array<const char *, N> &Keys, std::string_view Key)
{
	for(size_t i = 0; i < N; ++i) if(Key == Keys[i]) return static_cast<int>(i);
	return -1;
}
inline EParamKind ActionParamKind(EAction Action)
{
	switch(Action) { case ACT_SEND_CHAT: case ACT_RUN_COMMAND: case ACT_ECHO: return PARAM_STR; case ACT_STORE_VAR: case ACT_LOAD_VAR: return PARAM_VAR; case ACT_GET_VALUE: return PARAM_NONE; default: return PARAM_INT; }
}
inline bool ActionUsesGetValue(EAction Action) { return Action == ACT_GET_VALUE; }
inline bool ConditionUsesVarAndValue(ECondition Cond) { return Cond >= COND_VAR_EQ && Cond <= COND_VAR_GT; }
inline EParamKind ConditionParamKind(ECondition Cond) { return ConditionUsesVarAndValue(Cond) ? PARAM_VAR : Cond == COND_RANDOM_PROBABILITY ? PARAM_INT : PARAM_NONE; }
} // namespace modsystem
#endif
