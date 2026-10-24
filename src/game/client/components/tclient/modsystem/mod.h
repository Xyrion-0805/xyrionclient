#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_MOD_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_MOD_H
#include "mod_blocks.h"
#include <cstdint>
#include <string>
#include <vector>

namespace modsystem
{
using TId = uint64_t;
TId NewId();
constexpr int MAX_DEPTH = 8;
constexpr int MAX_NODES = 256;
constexpr int MAX_RULES = 64;
struct CCondition
{
	TId m_Id = NewId();
	ECondition m_Type = COND_NONE;
	ELogic m_Logic = LOGIC_AND; // AND has precedence; OR starts a new group.
	bool m_Not = false;
	std::string m_Left, m_Right = "0";
};
struct CBlock
{
	EAction m_Action = ACT_ECHO;
	std::string m_Argument;
	ENearestTarget m_Target = TARGET_SOLID;
	EGetValue m_Value = GETVAL_HOOK_ANGLE;
};
struct CScriptNode
{
	enum EKind { K_ACTION, K_IF_ELSE, K_LOOP, K_WAIT };
	TId m_Id = NewId();
	EKind m_Kind = K_ACTION;
	CBlock m_Block;
	std::vector<CCondition> m_vIfConds;
	std::vector<CScriptNode> m_vThen, m_vElse;
	bool m_HasElse = false;
	std::string m_Count = "1"; // loop count or wait ticks, evaluated at entry.
	bool HasChildren() const { return m_Kind == K_IF_ELSE || m_Kind == K_LOOP; }
};
struct CRule
{
	TId m_Id = NewId();
	EHook m_Hook = HOOK_ON_TICK;
	std::string m_Argument;
	std::vector<CCondition> m_vConditions;
	std::vector<CScriptNode> m_vBody;
};
struct CModVar { TId m_Id = NewId(); std::string m_Name, m_Initial = "0"; };
struct CModParam { TId m_Id = NewId(); std::string m_Name, m_Value = "0"; bool m_Numeric = true; };
struct CMod
{
	TId m_Id = NewId();
	std::string m_Name = "新模组";
	bool m_Enabled = true, m_Dirty = true;
	bool m_RemovedAimActions = false; // Loaded legacy actions were removed; review before enabling.
	uint64_t m_Revision = 0;
	std::string m_Filename, m_DiskText; // exact saved bytes detect reload conflicts.
	std::vector<CRule> m_vRules;
	std::vector<CModVar> m_vVars;
	std::vector<CModParam> m_vParams;
	void Touch() { m_Dirty = true; ++m_Revision; }
};
// Stable IDs are resolved afresh after vector mutations; no retained tree pointers.
struct SSequenceTarget { TId m_Rule = 0, m_Parent = 0; bool m_Else = false; };
CRule *FindRule(CMod &Mod, TId Id);
CScriptNode *FindNode(std::vector<CScriptNode> &Nodes, TId Id);
std::vector<CScriptNode> *ResolveSequence(CMod &Mod, const SSequenceTarget &Target);
bool ValidateMod(const CMod &Mod, std::string &Error);
bool MoveNode(CMod &Mod, TId Source, const SSequenceTarget &Target, size_t Index, std::string &Error);
bool InsertNode(CMod &Mod, CScriptNode Node, const SSequenceTarget &Target, size_t Index, std::string &Error);
bool DuplicateNode(CMod &Mod, TId Source, std::string &Error);
bool DuplicateRule(CMod &Mod, TId Source, TId &NewRule, std::string &Error);
bool DeleteNode(CMod &Mod, TId Id);
std::string SaveMod(const CMod &Mod);
bool LoadMod(std::string_view Text, CMod &Mod, std::string &Error);
std::string SafeFilename(std::string_view Name);
} // namespace modsystem
#endif
