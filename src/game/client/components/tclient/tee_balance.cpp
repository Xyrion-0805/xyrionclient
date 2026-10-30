#include "tee_balance.h"

#include <base/dbg.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/shared/config.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int VALIDATE_SETTLE_TICKS = 50;

bool Settled(CCharacter &Character)
{
	const auto &Core = *Character.Core();
	// Ground snapshots can retain one gravity step in vertical velocity.
	return Character.IsGrounded() && std::abs(Core.m_Vel.x) < 0.15f &&
		std::abs(Core.m_Vel.y) <= std::max(0.6f, std::abs((float)Core.m_Tuning.m_Gravity) + 0.1f);
}

class CCollisionTimeScope
{
	CCollision *m_pCollision;
	double m_Time;
public:
	explicit CCollisionTimeScope(CCollision *pCollision) : m_pCollision(pCollision), m_Time(pCollision->AxiomQuadTime()) {}
	~CCollisionTimeScope() { m_pCollision->SetAxiomQuadTime(m_Time); }
};

void SetFire(int &Counter, bool Press)
{
	if((Counter & 1) != (Press ? 1 : 0)) Counter = (Counter + 1) & INPUT_STATE_MASK;
}

void Neutralize(CGameWorld &World)
{
	World.m_WorldConfig.m_PredictEvents = false;
	World.m_WorldConfig.m_PredictWeapons = true;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		if(auto *pCharacter = World.GetCharacterById(Id))
		{
			CNetObj_PlayerInput Input{};
			Input.m_TargetY = -1;
			Input.m_Fire = (pCharacter->LatestInput()->m_Fire + 1) & (INPUT_STATE_MASK & ~1);
			pCharacter->SetInput(&Input);
			pCharacter->OnPredictedInput(&Input);
		}
}
}

CTeeBalance::CTeeBalance() = default;
CTeeBalance::~CTeeBalance() = default;

void CTeeBalance::SetStatus(const char *pText)
{
	str_copy(m_aStatus, pText);
	dbg_msg("tee_balance", "%s", m_aStatus);
}

void CTeeBalance::Stop(const char *pReason)
{
	const int64_t Begin = time_get_impl();
	if(Active())
	{
		auto &Input = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
		Input.m_Direction = Input.m_Hook = 0;
		SetFire(Input.m_Fire, false);
		SetStatus(pReason);
	}
	m_Stage = EStage::IDLE;
	g_Config.m_XcTeeBalanceRunning = 0;
	m_pPlanner.reset();
	m_pSnapshot.reset();
	m_pVerify.reset();
	m_pPositionWorld.reset();
	m_vApproach.clear();
	m_vStands.clear();
	m_TriedStands.clear();
	m_vActions.clear();
	m_vExpected.clear();
	m_MaxSliceUs = std::max(m_MaxSliceUs, (time_get_impl() - Begin) * 1000000 / time_freq());
}

void CTeeBalance::OnReset()
{
	Stop("已重置");
	m_ToggleHeld = false;
}

void CTeeBalance::ConToggle(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CTeeBalance *>(pUser);
	const bool Press = pResult->GetInteger(0) != 0;
	if(Press && !pSelf->m_ToggleHeld)
	{
		if(pSelf->Active()) pSelf->Stop("已手动停止");
		else pSelf->Start();
	}
	pSelf->m_ToggleHeld = Press;
}

void CTeeBalance::ConStatus(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CTeeBalance *>(pUser);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tee_balance", pSelf->Status());
	char aTiming[128];
	str_format(aTiming, sizeof(aTiming), "max_slice_us=%lld stands=%d tested=%zu", (long long)pSelf->m_MaxSliceUs,
		pSelf->m_PositionIndex + 1, pSelf->m_TriedStands.size());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tee_balance", aTiming);
	auto &World = pSelf->GameClient()->m_GameWorld;
	const auto *pLower = World.GetCharacterById(pSelf->m_Local);
	const auto *pUpper = World.GetCharacterById(pSelf->m_Target);
	if(pLower && pUpper)
	{
		char aLine[256];
		const auto &Input = pSelf->GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
		str_format(aLine, sizeof(aLine), "stage=%d tick=%d lower=%d pos=%.0f,%.0f vel=%.3f,%.3f upper=%d pos=%.0f,%.0f vel=%.3f,%.3f direction=%d hook=%d fire=%d",
			(int)pSelf->m_Stage, World.GameTick(), pSelf->m_Local, pLower->Core()->m_Pos.x, pLower->Core()->m_Pos.y,
			pLower->Core()->m_Vel.x, pLower->Core()->m_Vel.y, pSelf->m_Target, pUpper->Core()->m_Pos.x, pUpper->Core()->m_Pos.y,
			pUpper->Core()->m_Vel.x, pUpper->Core()->m_Vel.y, Input.m_Direction, Input.m_Hook, Input.m_Fire);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tee_balance", aLine);
	}
}

void CTeeBalance::OnConsoleInit()
{
	Console()->Register("+teebalance", "", CFGFLAG_CLIENT, ConToggle, this, "Start/stop lower-tee balance assistance");
	Console()->Register("teebalance_status", "", CFGFLAG_CLIENT, ConStatus, this, "Show tee balance status");
}

void CTeeBalance::Start()
{
	if(Active()) Stop("重新计算");
	if(!g_Config.m_XcTeeBalance) { SetStatus("请先在设置中启用动态平衡"); return; }
	if(Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Menus.IsActive() ||
		GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive() ||
		GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->IsWorldPaused()) { SetStatus("请在游戏画面中使用"); return; }
	auto &World = GameClient()->m_PredictedWorld;
	if(!World.m_WorldConfig.m_IsDDRace || World.m_WorldConfig.m_IsFNG)
	{ SetStatus("动态平衡当前仅支持 DDRace 物理"); return; }
	m_Local = GameClient()->m_Snap.m_LocalClientId;
	auto *pLocal = World.GetCharacterById(m_Local);
	if(!pLocal || !pLocal->IsGrounded() ||
		pLocal->m_FreezeTime || pLocal->Core()->m_DeepFrozen || pLocal->Core()->m_LiveFrozen ||
		pLocal->Core()->m_Solo || pLocal->Core()->m_Super || pLocal->Core()->m_CollisionDisabled || !pLocal->Core()->m_Tuning.m_PlayerCollision ||
		pLocal->Core()->m_HammerHitDisabled || pLocal->Core()->m_HookHitDisabled ||
		!pLocal->GetWeaponGot(WEAPON_HAMMER)) { SetStatus("请在可碰撞的地面上使用，并保有锤子"); return; }
	if(g_Config.m_ClDummyCopyMoves || g_Config.m_ClDummyHammer || g_Config.m_ClDummyControl || g_Config.m_DbgStress)
	{ SetStatus("请先关闭分身动作复制、自动锤击和控制"); return; }
	m_Target = -1;
	float Nearest = 384.0f;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		auto *pOther = World.GetCharacterById(Id);
		if(Id == m_Local || !pOther || !pLocal->CanCollide(Id) || !pOther->IsGrounded() ||
			!Settled(*pOther) || pOther->Core()->m_Solo || pOther->Core()->m_CollisionDisabled ||
			pOther->Core()->m_HookState != HOOK_IDLE || pOther->Core()->m_Super) continue;
		const float Distance = distance(pLocal->Core()->m_Pos, pOther->Core()->m_Pos);
		if(Distance < Nearest && std::abs(pOther->Core()->m_Pos.x - pLocal->Core()->m_Pos.x) >= 8)
		{ m_Target = Id; Nearest = Distance; }
	}
	if(m_Target < 0) { SetStatus("附近没有停稳且可碰撞的 Tee（范围 384）"); return; }
	auto *pTarget = World.GetCharacterById(m_Target);
	m_Direction = pTarget->Core()->m_Pos.x > pLocal->Core()->m_Pos.x ? 1 : -1;
	m_Connection = g_Config.m_ClDummy ? Client()->DummySelected() : 0;
	m_Team = GameClient()->m_Teams.Team(m_Local);
	m_Offset = vec2(0, 0);
	m_Rejected = 0;
	m_StartTick = -1;
	m_MaxSliceUs = 0;
	str_copy(m_aSearchFailure, "起手位置不可达");
	g_Config.m_XcPixelMove = 0;
	g_Config.m_XcTeeBalanceRunning = 1;
	m_PrepareTick = Client()->PredGameTick(g_Config.m_ClDummy);
	m_Stage = EStage::SETTLE;
	SetStatus("正在停稳并自动搜索可达起手位置…");
}

bool CTeeBalance::ValidateLive()
{
	if(!g_Config.m_XcTeeBalance || g_Config.m_XcPixelMove || Client()->State() != IClient::STATE_ONLINE ||
		GameClient()->m_Snap.m_LocalClientId != m_Local ||
		(g_Config.m_ClDummy ? Client()->DummySelected() : 0) != m_Connection ||
		GameClient()->m_Teams.Team(m_Local) != m_Team || GameClient()->IsWorldPaused() ||
		GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Menus.IsActive() ||
		GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive() ||
		g_Config.m_ClDummyCopyMoves || g_Config.m_ClDummyHammer || g_Config.m_ClDummyControl)
	{ Stop("已停止：游戏或操控状态变化"); return false; }
	auto &World = GameClient()->m_PredictedWorld;
	if(const char *pReason = UnsafeReason(World))
	{
		char aReason[256];
		str_format(aReason, sizeof(aReason), "已停止：%s", pReason);
		Stop(aReason);
		return false;
	}
	if(m_Stage == EStage::POSITION || m_Stage == EStage::SEARCH || m_Stage == EStage::VERIFY)
	{
		for(int Id : {m_Local, m_Target})
			if(distance(World.GetCharacterById(Id)->Core()->m_Pos, m_pSnapshot->GetCharacterById(Id)->Core()->m_Pos) > 2)
			{ Stop("角色已移动，请停稳后重新启动"); return false; }
	}
	return true;
}

bool CTeeBalance::Safe(CGameWorld &World) const
{
	return UnsafeReason(World) == nullptr;
}

const char *CTeeBalance::UnsafeReason(CGameWorld &World) const
{
	auto *pLower = World.GetCharacterById(m_Local);
	auto *pUpper = World.GetCharacterById(m_Target);
	if(!pLower || !pUpper) return "目标或自己已离开预测范围";
	if(pLower->m_FreezeTime || pLower->Core()->m_IsInFreeze || pLower->Core()->m_DeepFrozen || pLower->Core()->m_LiveFrozen)
		return "下方角色进入冻结区或仍被冻结";
	if(!pLower->CanCollide(m_Target) || pLower->Core()->m_CollisionDisabled || pUpper->Core()->m_CollisionDisabled ||
		!pLower->Core()->m_Tuning.m_PlayerCollision)
		return "两人之间的碰撞已关闭";
	if(pLower->Core()->m_ActiveWeapon == WEAPON_NINJA || pUpper->Core()->m_ActiveWeapon == WEAPON_NINJA)
		return "角色正在使用忍者武器";
	// Jetpack is an ability flag (and a fallback for missing DDNet snapshots).
	// It has no effect on an idle upper tee or on the lower tee's hammer.
	if(!pLower->IsGrounded()) return "下方角色脚下没有地面支撑";
	for(auto *pCharacter : {pLower, pUpper})
	{
		const vec2 Position = pCharacter->Core()->m_Pos;
		if(Position.x < 16 || Position.y < 16 || Position.x > Collision()->GetWidth() * 32 - 16 || Position.y > Collision()->GetHeight() * 32 - 16)
			return "角色接近地图边界";
		const int Index = Collision()->GetPureMapIndex(Position);
		if(Collision()->IsSpeedup(Index) || Collision()->IsTeleport(Index) || Collision()->IsEvilTeleport(Index) ||
			Collision()->IsCheckTeleport(Index) || Collision()->IsCheckEvilTeleport(Index))
			return "角色进入传送或加速区域";
		// Timing checkpoints, unfreeze, weapon abilities and other passive map
		// tiles are not obstacles. The complete world handles their actual effects.
		const auto Dangerous = [pCharacter, pLower](int Tile) {
			return Tile == TILE_DEATH || (pCharacter == pLower && (Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE));
		};
		if(Dangerous(Collision()->GetTileIndex(Index)) || Dangerous(Collision()->GetFrontTileIndex(Index)))
			return "角色进入冻结或死亡区域";
		for(int Tile : Collision()->GetAxiomQuadTiles(Position))
			if(Dangerous(Tile)) return "角色进入移动冻结或死亡区域";
		for(int Y : {-1, 1}) for(int X : {-1, 1})
		{
			const vec2 Probe = Position + vec2(X, Y) * (28.0f / 3);
			if(Collision()->GetCollisionAt(Probe.x, Probe.y) == TILE_DEATH || Collision()->GetFrontCollisionAt(Probe.x, Probe.y) == TILE_DEATH)
				return "角色碰到死亡区域";
		}
	}
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		if(Id != m_Local && Id != m_Target)
			if(const auto *pOther = World.GetCharacterById(Id); pOther)
				for(auto *pCharacter : {pLower, pUpper})
					if(pCharacter->CanCollide(Id) &&
						(distance(pOther->Core()->m_Pos, pCharacter->Core()->m_Pos) < CCharacterCore::PhysicalSize() * 1.25f ||
						pOther->Core()->HookedPlayer() == pCharacter->GetId() || pCharacter->Core()->HookedPlayer() == Id))
						return "其他玩家正在碰撞或钩住这两名角色";
	return nullptr;
}

void CTeeBalance::BeginPositionSearch()
{
	m_SearchStart = time_get_impl();
	auto &World = GameClient()->m_PredictedWorld;
	m_pSnapshot = std::make_unique<CGameWorld>();
	m_pSnapshot->CopyWorldClean(&World);
	Neutralize(*m_pSnapshot);
	const vec2 Lower = World.GetCharacterById(m_Local)->Core()->m_Pos;
	const vec2 Upper = World.GetCharacterById(m_Target)->Core()->m_Pos;
	m_Direction = Upper.x > Lower.x ? 1 : -1;
	m_vStands.clear();
	m_TriedStands.clear();
	const float CurrentGap = std::abs(Upper.x - Lower.x);
	const int PreferredDirection = m_Direction;
	for(int Direction : {PreferredDirection, -PreferredDirection})
	{
		const auto AddStand = [&](float Gap) {
			const SStand Stand{Upper.x - Direction * Gap, Direction};
			for(const auto &Other : m_vStands)
				if(Other.m_Direction == Direction && std::abs(Other.m_X - Stand.m_X) < 1) return;
			m_vStands.push_back(Stand);
		};
		if(Direction == PreferredDirection && CurrentGap >= 35 && CurrentGap <= 128) AddStand(CurrentGap);
		for(float Gap : {40.0f, 48.0f, 56.0f, 64.0f, 80.0f, 96.0f}) AddStand(Gap);
		for(int Gap = 36; Gap <= 128; Gap += 4) AddStand(Gap);
	}
	m_PositionIndex = -1;
	NextPosition();
}

void CTeeBalance::NextPosition()
{
	m_pPlanner.reset();
	m_pVerify.reset();
	m_pPositionWorld.reset();
	m_vApproach.clear();
	m_Rejected = 0;
	auto *pTarget = m_pSnapshot->GetCharacterById(m_Target);
	if(!pTarget) { Stop("目标已离开预测范围"); return; }
	while(++m_PositionIndex < (int)m_vStands.size())
	{
		m_Direction = m_vStands[m_PositionIndex].m_Direction;
		m_SetupPosition = vec2(m_vStands[m_PositionIndex].m_X,
			m_pSnapshot->GetCharacterById(m_Local)->Core()->m_Pos.y);
		if(Collision()->TestBox(m_SetupPosition, CCharacterCore::PhysicalSizeVec2()) ||
			!Collision()->IsOnGround(m_SetupPosition, CCharacterCore::PhysicalSize())) continue;
		m_StandX = m_SetupPosition.x;
		m_PositionTicks = 0;
		m_StallTicks = 0;
		m_pPositionWorld = std::make_unique<CGameWorld>();
		m_pPositionWorld->CopyWorldClean(m_pSnapshot.get());
		m_pPositionWorld->m_LocalClientId = m_Local;
		m_SimFire = m_pPositionWorld->GetCharacterById(m_Local)->LatestInput()->m_Fire;
		SetFire(m_SimFire, false);
		m_Stage = EStage::POSITION;
		if(m_PositionIndex == 0) SetStatus("正在搜索左右可达站位与钩锤方案…");
		return;
	}
	char aReason[256];
	str_format(aReason, sizeof(aReason), "未找到平衡方案：%s", m_aSearchFailure);
	Stop(aReason);
}

void CTeeBalance::PositionStep()
{
	auto &World = *m_pPositionWorld;
	auto *pLower = World.GetCharacterById(m_Local);
	auto *pUpper = World.GetCharacterById(m_Target);
	if(const char *pReason = UnsafeReason(World))
	{
		str_copy(m_aSearchFailure, pReason);
		NextPosition();
		return;
	}
	if(m_PositionTicks >= 256)
	{
		str_copy(m_aSearchFailure, "行走路线被阻挡或当前调参下无法自然停稳");
		NextPosition();
		return;
	}
	const auto &Core = *pLower->Core();
	const float Error = m_StandX - Core.m_Pos.x;
	if(std::abs(Error) <= 8 && std::abs(Core.m_Vel.x) < 0.1f && std::abs(Core.m_Pos.y - m_SetupPosition.y) < 2)
	{
		m_SetupPosition = Core.m_Pos;
		const std::array<int, 5> Key{round_to_int(Core.m_Pos.x), round_to_int(Core.m_Pos.y),
			round_to_int(pUpper->Core()->m_Pos.x), round_to_int(pUpper->Core()->m_Pos.y), m_Direction};
		if(!m_TriedStands.insert(Key).second) { NextPosition(); return; }
		bool Reverse = false;
		for(auto *pEntity = World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pEntity; pEntity = pEntity->TypeNext())
		{
			if(pEntity == pUpper) { Reverse = true; break; }
			if(pEntity == pLower) break;
		}
		TeeBalance::CSimulation Simulation(Collision(), Core, *pUpper->Core(), *World.Teams(), World.Switchers(), Reverse, World.m_WorldConfig.m_NoWeakHookAndBounce);
		m_pPlanner = std::make_unique<TeeBalance::CPlanner>(Simulation, m_Direction);
		m_Stage = EStage::SEARCH;
		return;
	}
	// Approach with normal walking and coast to a stop. Do not use the
	// opposite direction to brake; candidate routes are checked by physics.
	const int Direction = TeeBalance::WalkingDirection(Core, m_StandX);
	const float BeforeX = Core.m_Pos.x;
	CNetObj_PlayerInput Input{};
	Input.m_TargetY = -1;
	Input.m_Direction = Direction;
	Input.m_WantedWeapon = WEAPON_HAMMER + 1;
	Input.m_Fire = m_SimFire;
	++World.m_GameTick;
	Collision()->SetAxiomQuadTick(World.GameTick(), SERVER_TICK_SPEED);
	pLower->OnDirectInput(&Input);
	pLower->OnPredictedInput(&Input);
	World.Tick();
	pLower = World.GetCharacterById(m_Local);
	if(pLower && Direction && std::abs(pLower->Core()->m_Pos.x - BeforeX) < 0.25f) ++m_StallTicks;
	else m_StallTicks = 0;
	if(m_StallTicks >= 10)
	{
		str_copy(m_aSearchFailure, "行走路线被墙体或其他角色阻挡");
		NextPosition();
		return;
	}
	m_vApproach.push_back({Direction, false, false});
	++m_PositionTicks;
}

void CTeeBalance::BeginVerification()
{
	m_vActions = m_vApproach;
	m_vActions.insert(m_vActions.end(), m_pPlanner->Result().begin(), m_pPlanner->Result().end());
	m_pVerify = std::make_unique<CGameWorld>();
	m_pVerify->CopyWorldClean(m_pSnapshot.get());
	m_pVerify->m_LocalClientId = m_Local;
	m_SimFire = m_pVerify->GetCharacterById(m_Local)->LatestInput()->m_Fire;
	m_VerifyTick = 0;
	m_VerifyStableTicks = 0;
	m_vExpected.clear();
	m_Stage = EStage::VERIFY;
}

void CTeeBalance::VerifyStep()
{
	auto &World = *m_pVerify;
	auto *pLower = World.GetCharacterById(m_Local);
	auto *pUpper = World.GetCharacterById(m_Target);
	if(!pLower || !pUpper) { Stop("完整预测无法读取角色"); return; }
	if(m_VerifyTick < (int)m_vActions.size())
		m_vExpected.push_back({pLower->Core()->m_Pos, pUpper->Core()->m_Pos});
	const auto Action = m_VerifyTick < (int)m_vActions.size() ? m_vActions[m_VerifyTick] : TeeBalance::SAction{m_Direction, false, false};
	CNetObj_PlayerInput Input{};
	Input.m_Direction = Action.m_Direction;
	Input.m_Hook = Action.m_Hook;
	Input.m_WantedWeapon = WEAPON_HAMMER + 1;
	const vec2 Aim = normalize(pUpper->Core()->m_Pos - pLower->Core()->m_Pos);
	Input.m_TargetX = round_to_int(Aim.x * 1000);
	Input.m_TargetY = round_to_int(Aim.y * 1000);
	SetFire(m_SimFire, Action.m_Hammer);
	Input.m_Fire = m_SimFire;
	++World.m_GameTick;
	Collision()->SetAxiomQuadTick(World.GameTick(), SERVER_TICK_SPEED);
	pLower->OnDirectInput(&Input);
	pLower->OnPredictedInput(&Input);
	World.Tick();
	++m_VerifyTick;
	pLower = World.GetCharacterById(m_Local);
	pUpper = World.GetCharacterById(m_Target);
	if(!pLower || !pUpper) { NextPosition(); return; }
	const char *pUnsafe = UnsafeReason(World);
	const bool FinishedActions = m_VerifyTick >= (int)m_vActions.size();
	if(FinishedActions && TeeBalance::InFormation(*pLower->Core(), *pUpper->Core(), m_Direction)) ++m_VerifyStableTicks;
	else m_VerifyStableTicks = 0;
	if(pUnsafe || (m_VerifyTick >= (int)m_vActions.size() + VALIDATE_SETTLE_TICKS && m_VerifyStableTicks < 4))
	{
		str_copy(m_aSearchFailure, pUnsafe ? pUnsafe : "钩锤后无法持续保持平衡");
		m_pVerify.reset();
		if(++m_Rejected >= 8) { NextPosition(); return; }
		m_pPlanner->RejectResult();
		m_Stage = EStage::SEARCH;
		return;
	}
	// Verify the actual setup and its immediate convergence. The long-term
	// stability probe stays over the local ground; it is not a runway test.
	if(FinishedActions && m_VerifyStableTicks >= 4)
	{
		m_Offset = pUpper->Core()->m_Pos - pLower->Core()->m_Pos;
		m_Stage = EStage::EXECUTE;
		m_StartTick = -1;
		m_pVerify.reset();
		m_pPlanner.reset();
		m_pSnapshot.reset();
		SetStatus("已找到起手位置，正在自动移动并建立平衡…");
	}
}

void CTeeBalance::OnUpdate()
{
	if(m_ToggleHeld)
	{
		bool Held = false;
		for(int Key = 1; Key < KEY_LAST && !Held; ++Key)
			if(Input()->KeyIsPressed(Key))
				for(int Modifier = 0; Modifier < KeyModifier::COMBINATION_COUNT && !Held; ++Modifier)
					Held = str_find(GameClient()->m_Binds.Get(Key, Modifier), "+teebalance") != nullptr;
		if(!Held) m_ToggleHeld = false;
	}
	if(!Active() || !ValidateLive()) return;
	if((m_Stage == EStage::POSITION || m_Stage == EStage::SEARCH || m_Stage == EStage::VERIFY) &&
		time_get_impl() - m_SearchStart > time_freq() * 5)
	{
		Stop("本次搜索未找到可行方案，可调整站位后重试");
		return;
	}
	CCollisionTimeScope TimeScope(Collision());
	const int64_t Begin = time_get_impl();
	// time_get() is cached for the frame and cannot enforce a work deadline.
	const int64_t Deadline = Begin + time_freq() / 1000;
	do
	{
		if(m_Stage == EStage::SETTLE)
		{
			auto &World = GameClient()->m_PredictedWorld;
			auto *pLower = World.GetCharacterById(m_Local);
			if(Settled(*pLower) && Settled(*World.GetCharacterById(m_Target)) &&
				pLower->GetActiveWeapon() == WEAPON_HAMMER && pLower->GetReloadTimer() == 0) BeginPositionSearch();
			else if(Client()->PredGameTick(g_Config.m_ClDummy) - m_PrepareTick > 150) Stop("角色未能停稳，请松开手动操作后重试");
			break;
		}
		else if(m_Stage == EStage::POSITION) PositionStep();
		else if(m_Stage == EStage::SEARCH)
		{
			Collision()->SetAxiomQuadTick(m_pSnapshot->GameTick(), SERVER_TICK_SPEED);
			m_pPlanner->Step(4);
			if(m_pPlanner->Finished())
			{
				if(!m_pPlanner->Found())
				{
					str_copy(m_aSearchFailure, "当前站位与调参下未找到可持续的钩锤时序");
					NextPosition();
					continue;
				}
				BeginVerification();
			}
		}
		else if(m_Stage == EStage::VERIFY) VerifyStep();
		else break;
	} while(Active() && time_get_impl() < Deadline);
	m_MaxSliceUs = std::max(m_MaxSliceUs, (time_get_impl() - Begin) * 1000000 / time_freq());
}

void CTeeBalance::ApplyInput(CNetObj_PlayerInput &Input)
{
	if(!Active() || !ValidateLive()) return;
	Input.m_Direction = Input.m_Hook = 0;
	Input.m_Jump = 0;
	SetFire(Input.m_Fire, false);
	if(m_Stage == EStage::SETTLE || m_Stage == EStage::POSITION || m_Stage == EStage::SEARCH || m_Stage == EStage::VERIFY)
	{
		Input.m_WantedWeapon = WEAPON_HAMMER + 1;
		return;
	}
	const int Tick = Client()->PredGameTick(g_Config.m_ClDummy);
	if(m_StartTick < 0) { m_StartTick = Tick; m_Fire = Input.m_Fire; }
	const int Offset = Tick - m_StartTick;
	if(Offset < 0) { Stop("预测时间已重置"); return; }
	auto &World = GameClient()->m_PredictedWorld;
	auto *pLower = World.GetCharacterById(m_Local);
	auto *pUpper = World.GetCharacterById(m_Target);
	if(m_Stage == EStage::EXECUTE && Offset >= (int)m_vActions.size())
	{
		m_Stage = EStage::HOLD;
		SetStatus(m_Direction > 0 ? "保持平衡：持续按住向右" : "保持平衡：持续按住向左");
	}
	if(m_Stage == EStage::HOLD)
	{
		// Never insert neutral frames or reverse direction while maintaining.
		if(Offset > (int)m_vActions.size() + 50 && !TeeBalance::InFormation(*pLower->Core(), *pUpper->Core(), m_Direction))
		{ Stop("平衡已被打破，按快捷键重新建立"); return; }
		Input.m_Direction = m_Direction;
		SetFire(m_Fire, false);
		Input.m_Fire = m_Fire;
		return;
	}
	const auto &Action = m_vActions[Offset];
	Input.m_Direction = Action.m_Direction;
	Input.m_Hook = Action.m_Hook;
	Input.m_WantedWeapon = WEAPON_HAMMER + 1;
	// Replay the aim that was validated for this tick. The displayed predicted
	// world can already contain a further tick of the previous input.
	const vec2 Aim = normalize(m_vExpected[Offset].m_Upper - m_vExpected[Offset].m_Lower);
	Input.m_TargetX = round_to_int(Aim.x * 1000);
	Input.m_TargetY = round_to_int(Aim.y * 1000);
	SetFire(m_Fire, Action.m_Hammer);
	Input.m_Fire = m_Fire;
}

void CTeeBalance::OnRender()
{
	if(!Active() || GameClient()->IsRenderingDummyMiniMap() || Client()->State() != IClient::STATE_ONLINE) return;
	const CScreenRect Previous = Graphics()->GetScreen();
	Graphics()->MapScreen(0, 0, 300 * Graphics()->ScreenAspect(), 300);
	TextRender()->TextColor(0.8f, 1, 0.85f, 1);
	TextRender()->Text(12, 55, 7, m_aStatus);
	TextRender()->TextColor(1, 1, 1, 1);
	Graphics()->MapScreen(Previous);
}
