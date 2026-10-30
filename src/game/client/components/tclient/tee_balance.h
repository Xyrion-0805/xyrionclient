#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TEE_BALANCE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TEE_BALANCE_H

#include <game/client/component.h>
#include <game/tee_balance_physics.h>
#include <engine/console.h>

#include <memory>
#include <vector>

class CGameWorld;

class CTeeBalance : public CComponent
{
public:
	CTeeBalance();
	~CTeeBalance() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnReset() override;
	void OnUpdate() override;
	void OnRender() override;
	void Start();
	void Stop(const char *pReason);
	void ApplyInput(CNetObj_PlayerInput &Input);
	bool Active() const { return m_Stage != EStage::IDLE; }
	const char *Status() const { return m_aStatus; }
	vec2 Offset() const { return m_Offset; }

private:
	enum class EStage { IDLE, SEARCH, VERIFY, EXECUTE, HOLD, SETTLE, POSITION };
	struct SPose { vec2 m_Lower, m_Upper; };
	struct SStand { float m_X; int m_Direction; };
	EStage m_Stage = EStage::IDLE;
	int m_Local = -1, m_Target = -1, m_Connection = -1, m_Team = -1;
	int m_Direction = 0, m_StartTick = -1, m_VerifyTick = 0, m_Rejected = 0;
	int m_Fire = 0, m_SimFire = 0;
	int m_VerifyStableTicks = 0;
	int m_PositionIndex = 0, m_PositionTicks = 0, m_PrepareTick = 0;
	int m_StallTicks = 0;
	int64_t m_MaxSliceUs = 0;
	int64_t m_SearchStart = 0;
	float m_StandX = 0;
	bool m_ToggleHeld = false;
	char m_aStatus[192] = "待命";
	char m_aSearchFailure[192] = "起手位置不可达";
	vec2 m_Offset = vec2(0, 0);
	vec2 m_SetupPosition = vec2(0, 0);
	std::unique_ptr<TeeBalance::CPlanner> m_pPlanner;
	std::unique_ptr<CGameWorld> m_pSnapshot, m_pVerify;
	std::unique_ptr<CGameWorld> m_pPositionWorld;
	std::vector<SStand> m_vStands;
	std::set<std::array<int, 5>> m_TriedStands;
	std::vector<TeeBalance::SAction> m_vApproach;
	std::vector<TeeBalance::SAction> m_vActions;
	std::vector<SPose> m_vExpected;
	bool ValidateLive();
	const char *UnsafeReason(CGameWorld &World) const;
	bool Safe(CGameWorld &World) const;
	void BeginPositionSearch();
	void NextPosition();
	void PositionStep();
	void BeginVerification();
	void VerifyStep();
	void SetStatus(const char *pText);
	static void ConToggle(IConsole::IResult *pResult, void *pUser);
	static void ConStatus(IConsole::IResult *pResult, void *pUser);
};

#endif
