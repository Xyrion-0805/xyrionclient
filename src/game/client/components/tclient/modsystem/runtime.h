#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_RUNTIME_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_RUNTIME_H
#include "expression.h"
#include "mod.h"
#include <map>
#include <memory>

namespace modsystem
{
struct SNearest
{
	bool m_Valid = false;
	double m_X = 0, m_Y = 0, m_Distance = 0, m_Angle = 0;
	int m_Player = -1;
};
struct SRuntimeState
{
	std::map<std::string, double, std::less<>> m_Variables;
	double m_Register = 0;
	SNearest m_Nearest;
	uint64_t m_Revision = 0;
	std::string m_Error;
};
bool ResolveNumber(const CMod &Mod, SRuntimeState &State, std::string_view Expression, double &Value);
std::string ResolveText(const CMod &Mod, SRuntimeState &State, std::string_view Text);
class IRuntimeHost
{
public:
	virtual ~IRuntimeHost() = default;
	virtual bool Condition(const CCondition &Condition, const CMod &Mod, SRuntimeState &State) = 0;
	virtual void Action(const CBlock &Block, const CMod &Mod, SRuntimeState &State) = 0;
};
bool EvaluateConditions(const std::vector<CCondition> &Conditions, const CMod &Mod, SRuntimeState &State, IRuntimeHost &Host);
class CExecutor
{
	struct SFrame { const std::vector<CScriptNode> *m_pNodes; size_t m_Index = 0; int m_Remaining = 1; };
	struct STask
	{
		TId m_Mod, m_Rule;
		uint64_t m_Revision;
		std::shared_ptr<const std::vector<CScriptNode>> m_pBody;
		std::vector<SFrame> m_Stack;
		int64_t m_Wake = 0;
		int m_Executed = 0;
	};
	std::vector<STask> m_vTasks;
	std::map<TId, SRuntimeState> m_States;
	int64_t m_BudgetTick = -1;
	int m_TickBudget = MAX_NODES;
	SRuntimeState &EnsureState(const CMod &Mod);
public:
	void Fire(const std::vector<CMod> &Mods, EHook Hook, std::string_view Argument, int64_t Tick, IRuntimeHost &Host);
	void Advance(const std::vector<CMod> &Mods, int64_t Tick, IRuntimeHost &Host);
	void Cancel(TId Mod = 0);
	void Reset();
	const SRuntimeState *State(TId Id) const;
	size_t TaskCount() const { return m_vTasks.size(); }
};
} // namespace modsystem
#endif
