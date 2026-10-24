#include "runtime.h"
#include <base/secure.h>
#include <algorithm>
#include <cmath>

namespace modsystem
{
namespace
{
bool Number(const CMod &Mod, SRuntimeState &State, std::string_view Expression, double &Value, int Depth)
{
	if(Depth > 16) { State.m_Error = "参数互相引用或深度超过 16"; return false; }
	return SExprEval::Eval(Expression, State.m_Register, [&](std::string_view Name, double &Out) {
		auto It = State.m_Variables.find(Name); if(It != State.m_Variables.end()) { Out = It->second; return true; }
		for(const auto &P : Mod.m_vParams) if(P.m_Name == Name && P.m_Numeric) return Number(Mod, State, P.m_Value, Out, Depth + 1);
		return false;
	}, Value, &State.m_Error);
}
const CMod *FindMod(const std::vector<CMod> &Mods, TId Id) { for(const auto &M : Mods) if(M.m_Id == Id) return &M; return nullptr; }
}
bool ResolveNumber(const CMod &Mod, SRuntimeState &State, std::string_view Expression, double &Value) { return Number(Mod, State, Expression, Value, 0); }
std::string ResolveText(const CMod &Mod, SRuntimeState &State, std::string_view Text)
{
	return Substitute(Text, State.m_Register, [&](std::string_view Name, double &Out) { return ResolveNumber(Mod, State, Name, Out); }, [&](std::string_view Name, std::string &Out) { for(const auto &P : Mod.m_vParams) if(P.m_Name == Name && !P.m_Numeric) { Out = P.m_Value; return true; } return false; });
}
bool EvaluateConditions(const std::vector<CCondition> &Conditions, const CMod &Mod, SRuntimeState &State, IRuntimeHost &Host)
{
	// OR separates AND groups. NOT applies to its row; the empty list is true.
	bool Result = false, Group = true;
	for(size_t i = 0; i < Conditions.size(); ++i)
	{
		const auto &C = Conditions[i];
		if(i && C.m_Logic == LOGIC_OR) { Result = Result || Group; Group = true; }
		bool Value;
		if(C.m_Type == COND_NONE) Value = true;
		else if(C.m_Type == COND_RANDOM_PROBABILITY)
		{
			double Probability = 0;
			if(!ResolveNumber(Mod, State, C.m_Right, Probability)) return false;
			Probability = std::clamp(Probability, 0.0, 100.0);
			Value = Probability >= 100.0 || (Probability > 0.0 && secure_rand_below(10000) < Probability * 100.0);
		}
		else if(ConditionUsesVarAndValue(C.m_Type))
		{
			double L = 0, R = 0;
			bool Ok = ResolveNumber(Mod, State, C.m_Left, L) && ResolveNumber(Mod, State, C.m_Right, R);
			if(!Ok) return false; // 无效表达式不能因 NOT 而变成通过条件。
			Value = C.m_Type == COND_VAR_EQ ? L == R : C.m_Type == COND_VAR_LT ? L < R : L > R;
		}
		else Value = Host.Condition(C, Mod, State);
		Group = Group && (C.m_Not ? !Value : Value);
	}
	return Result || Group;
}
SRuntimeState &CExecutor::EnsureState(const CMod &Mod)
{
	auto [It, New] = m_States.try_emplace(Mod.m_Id);
	auto &S = It->second;
	if(New || S.m_Revision != Mod.m_Revision)
	{
		S = SRuntimeState{}; S.m_Revision = Mod.m_Revision;
		for(const auto &V : Mod.m_vVars) S.m_Variables[V.m_Name] = 0;
		for(const auto &V : Mod.m_vVars) { double Value = 0; if(ResolveNumber(Mod, S, V.m_Initial, Value)) S.m_Variables[V.m_Name] = Value; }
	}
	return S;
}
void CExecutor::Fire(const std::vector<CMod> &Mods, EHook Hook, std::string_view Argument, int64_t Tick, IRuntimeHost &Host)
{
	for(const auto &M : Mods)
	{
		if(!M.m_Enabled) { Cancel(M.m_Id); continue; }
		auto &S = EnsureState(M);
		std::string ValidationError;
		if(!ValidateMod(M, ValidationError))
		{
			Cancel(M.m_Id);
			S.m_Error = ValidationError;
			continue;
		}
		for(const auto &R : M.m_vRules)
		{
			if(R.m_Hook != Hook || (Hook == HOOK_ON_KEY_PRESS && R.m_Argument != Argument)) continue;
			// A waiting OnTick rule has one activation, rather than accumulating 50 tasks/s.
			bool Running = std::any_of(m_vTasks.begin(), m_vTasks.end(), [&](const STask &T) { return T.m_Mod == M.m_Id && T.m_Rule == R.m_Id && T.m_Revision == M.m_Revision; });
			if(Running || m_vTasks.size() >= 64 || !EvaluateConditions(R.m_vConditions, M, S, Host)) continue;
			STask T; T.m_Mod = M.m_Id; T.m_Rule = R.m_Id; T.m_Revision = M.m_Revision; T.m_pBody = std::make_shared<const std::vector<CScriptNode>>(R.m_vBody); T.m_Stack.push_back({T.m_pBody.get(), 0, 1}); T.m_Wake = Tick; m_vTasks.push_back(std::move(T));
		}
	}
	Advance(Mods, Tick, Host);
}
void CExecutor::Advance(const std::vector<CMod> &Mods, int64_t Tick, IRuntimeHost &Host)
{
	if(m_BudgetTick != Tick) { m_BudgetTick = Tick; m_TickBudget = MAX_NODES; }
	for(auto &T : m_vTasks)
	{
		const auto *pMod = FindMod(Mods, T.m_Mod);
		if(!pMod || !pMod->m_Enabled || pMod->m_Revision != T.m_Revision) { T.m_Stack.clear(); continue; }
		if(T.m_Wake > Tick) continue;
		auto &S = EnsureState(*pMod);
		while(!T.m_Stack.empty() && m_TickBudget > 0)
		{
			auto &F = T.m_Stack.back();
			if(F.m_Index == F.m_pNodes->size())
			{
				if(--F.m_Remaining > 0 && !F.m_pNodes->empty()) F.m_Index = 0; else T.m_Stack.pop_back();
				continue;
			}
			if(T.m_Executed >= MAX_NODES) { S.m_Error = "本次规则执行达到 256 节点限制"; T.m_Stack.clear(); break; }
			const auto &N = (*F.m_pNodes)[F.m_Index++]; ++T.m_Executed; --m_TickBudget;
			if(N.m_Kind == CScriptNode::K_ACTION)
			{
				if(N.m_Block.m_Action == ACT_STORE_VAR)
				{
					const auto &Name = N.m_Block.m_Argument;
					bool Param = std::any_of(pMod->m_vParams.begin(), pMod->m_vParams.end(), [&](const CModParam &P) { return P.m_Name == Name; });
					if(!Param && !Name.empty() && Name.size() <= 128 && (S.m_Variables.size() < 256 || S.m_Variables.count(Name))) S.m_Variables[Name] = S.m_Register;
					else S.m_Error = "不能写入参数、空变量或超过变量限制";
				}
				else if(N.m_Block.m_Action == ACT_LOAD_VAR) { double V; if(ResolveNumber(*pMod, S, N.m_Block.m_Argument, V)) S.m_Register = V; }
				else Host.Action(N.m_Block, *pMod, S);
			}
			else if(N.m_Kind == CScriptNode::K_IF_ELSE)
			{
				bool Test = EvaluateConditions(N.m_vIfConds, *pMod, S, Host);
				const auto &Body = Test ? N.m_vThen : N.m_vElse;
				if((Test || N.m_HasElse || !Body.empty()) && !Body.empty()) T.m_Stack.push_back({&Body, 0, 1});
			}
			else
			{
				double Count = 0; if(!ResolveNumber(*pMod, S, N.m_Count, Count)) continue;
				int Bounded = static_cast<int>(std::clamp(std::trunc(Count), 0.0, 1000000.0));
				if(N.m_Kind == CScriptNode::K_LOOP) { if(Bounded && !N.m_vThen.empty()) T.m_Stack.push_back({&N.m_vThen, 0, Bounded}); }
				else if(Bounded) { T.m_Wake = Tick + Bounded; break; }
			}
		}
	}
	m_vTasks.erase(std::remove_if(m_vTasks.begin(), m_vTasks.end(), [](const STask &T) { return T.m_Stack.empty(); }), m_vTasks.end());
}
void CExecutor::Cancel(TId Mod) { if(!Mod) m_vTasks.clear(); else m_vTasks.erase(std::remove_if(m_vTasks.begin(), m_vTasks.end(), [Mod](const STask &T) { return T.m_Mod == Mod; }), m_vTasks.end()); }
void CExecutor::Reset() { Cancel(); m_States.clear(); m_BudgetTick = -1; m_TickBudget = MAX_NODES; }
const SRuntimeState *CExecutor::State(TId Id) const { auto It = m_States.find(Id); return It == m_States.end() ? nullptr : &It->second; }
} // namespace modsystem
