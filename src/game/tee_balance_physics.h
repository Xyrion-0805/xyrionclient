#ifndef GAME_TEE_BALANCE_PHYSICS_H
#define GAME_TEE_BALANCE_PHYSICS_H

#include <game/gamecore.h>
#include <game/teamscore.h>

#include <memory>
#include <array>
#include <set>
#include <vector>

namespace TeeBalance
{
struct SAction
{
	int m_Direction = 0;
	bool m_Hook = false;
	bool m_Hammer = false;
	bool m_Jump = false;
};

bool InFormation(const CCharacterCore &Lower, const CCharacterCore &Upper, int Direction);
int WalkingDirection(const CCharacterCore &Core, float StandX);

// Detached two-player core simulation. No writes to the source world or cores.
class CSimulation
{
	CCollision *m_pCollision;
	CWorldCore m_World;
	CTeamsCore m_Teams;
	CCharacterCore m_aCores[2];
	bool m_Reverse;
	bool m_NoWeak;
	int m_ReloadTicks = 0;

public:
	CSimulation(CCollision *pCollision, const CCharacterCore &Lower, const CCharacterCore &Upper,
		const CTeamsCore &Teams, const std::vector<SSwitchers> &Switchers, bool Reverse, bool NoWeak,
		int ReloadTicks = 0, bool PreserveHook = false);
	CSimulation(const CSimulation &Other);
	CSimulation &operator=(const CSimulation &) = delete;
	void Tick(SAction Action);
	void AnchorHorizontal(float LowerX);
	const CCharacterCore &Lower() const { return m_aCores[0]; }
	const CCharacterCore &Upper() const { return m_aCores[1]; }
	bool CanHammer() const { return m_ReloadTicks <= 1; }
	int ReloadTicks() const { return m_ReloadTicks; }
};

class CPlanner
{
	struct SNode
	{
		std::unique_ptr<CSimulation> m_pSimulation;
		std::vector<SAction> m_vActions;
		int m_Hammers = 0;
		float m_Score = 0;
		unsigned m_Order = 0;
	};
	std::vector<SNode> m_vBeam;
	std::vector<SNode> m_vNext;
	std::set<std::array<int, 9>> m_Seen;
	std::unique_ptr<CSimulation> m_pProbe;
	size_t m_ProbeIndex = 0, m_ExpandIndex = 0;
	int m_ProbeTick = 0, m_ActionIndex = 0;
	float m_ProbeAnchorX = 0;
	unsigned m_Order = 0;
	bool m_Expanding = false;
	std::vector<SAction> m_vResult;
	int m_Direction;
	int m_Depth = 0;
	bool m_Finished = false;
	vec2 m_Offset = vec2(0, 0);
	float Score(const CSimulation &Simulation) const;

public:
	CPlanner(const CSimulation &Initial, int Direction);
	// Each unit performs at most one simulation tick, including hold probes.
	void Step(int WorkBudget = 32);
	bool Finished() const { return m_Finished; }
	bool Found() const { return !m_vResult.empty(); }
	int Depth() const { return m_Depth; }
	const std::vector<SAction> &Result() const { return m_vResult; }
	vec2 Offset() const { return m_Offset; }
	void RejectResult() { m_vResult.clear(); m_Finished = false; }
};
}

#endif
