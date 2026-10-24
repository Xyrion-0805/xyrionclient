#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_RESCUE_SOLVER_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_RESCUE_SOLVER_H

#include <base/vmath.h>
#include <game/client/component.h>

#include <cstdint>
#include <memory>
#include <vector>

class CCharacter;
class CGameWorld;
class CRescueNavigation;

// Advisory only: the solver never writes to controls or sends player inputs.
class CRescueSolver : public CComponent
{
public:
	struct CResult
	{
		bool m_Valid = false;
		bool m_Grounded = false;
		vec2 m_Position = vec2(0, 0);
		vec2 m_Direction = vec2(1, 0);
		int m_MouseX = 0;
		int m_MouseY = 0;
		float m_Angle = 0;
		std::vector<vec2> m_vApproach;
	};

	CRescueSolver();
	~CRescueSolver() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;
	void OnRender() override;
	void OnNewSnapshot() override;
	bool IsCandidate(int ClientId) const;
	void SelectTarget(int ClientId);
	int Target() const { return m_Target; }
	void Start();
	void Clear();
	bool Busy() const { return m_Stage != EStage::IDLE; }
	int Progress() const;
	const char *Status() const { return m_aStatus; }
	const CResult &Result() const { return m_Result; }

private:
	friend class CRescueNavigation;
	enum class EStage { IDLE, NAVIGATE, AIR_SEARCH, SIMULATE };
	struct CCandidate
	{
		vec2 m_Position;
		vec2 m_Direction;
		bool m_Grounded;
		int m_NavigationNode;
	};
	EStage m_Stage = EStage::IDLE;
	int m_Target = -1;
	int m_LocalId = -1;
	int m_Dummy = -1;
	int m_Team = -1;
	int m_StartTick = 0;
	int m_SimTick = 0;
	bool m_SearchingAir = false;
	float m_Range = 0;
	double m_QuadTime = 0;
	int64_t m_StartedAt = 0;
	vec2 m_TargetPos = vec2(0, 0);
	vec2 m_LocalPos = vec2(0, 0);
	vec2 m_LastLocalPos = vec2(0, 0);
	char m_aTargetName[64] = {};
	char m_aStatus[192] = {};
	std::unique_ptr<CGameWorld> m_pSnapshot;
	std::unique_ptr<CGameWorld> m_pSimulation;
	std::unique_ptr<CRescueNavigation> m_pNavigation;
	std::vector<CCandidate> m_vCandidates;
	CResult m_Result;

	bool Frozen(const CCharacter *pCharacter) const;
	bool FreezeTile(int Index, CGameWorld &World) const;
	bool Hazard(vec2 Position, CGameWorld &World, bool IncludeFreeze) const;
	bool UnsupportedTile(vec2 Position) const;
	bool SafePosition(vec2 Position, CGameWorld &World) const;
	bool HookRay(vec2 Position, vec2 Direction, float HookLength, float HookSpeed) const;
	bool NavigationSegment(vec2 From, vec2 To) const;
	bool SearchPosition(int NavigationNode);
	void BeginSimulation();
	bool SimulateTick();
	void FinishCandidate();
	void Finish();
	bool Validate();
	void Invalidate(const char *pReason);
};

#endif
