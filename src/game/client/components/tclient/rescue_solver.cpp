#include "rescue_solver.h"

#include <base/str.h>
#include <base/time.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/mapitems.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace
{
constexpr int MAX_FLIGHT_TICKS = 64;
constexpr int MAX_NAVIGATION_STATES = 3072;
constexpr int NAVIGATION_ACTION_TICKS = 4;
constexpr int MAX_NAVIGATION_TICKS = 200;

// Collision belongs to the live client, even in a detached prediction world.
// Always restore its animation time before returning to normal prediction/rendering.
class CCollisionTimeScope
{
	CCollision *m_pCollision;
	double m_Time;
public:
	explicit CCollisionTimeScope(CCollision *pCollision) : m_pCollision(pCollision), m_Time(pCollision->AxiomQuadTime()) {}
	~CCollisionTimeScope() { m_pCollision->SetAxiomQuadTime(m_Time); }
};

void Neutralize(CGameWorld &World)
{
	World.m_WorldConfig.m_PredictWeapons = false;
	World.m_WorldConfig.m_PredictEvents = false;
	World.m_WorldConfig.m_PredictTiles = true;
	World.m_WorldConfig.m_PredictDDRace = true;
	World.m_WorldConfig.m_PredictFreeze = 1;
	World.m_PredictedEvents.clear();
	// Keep character order/strong hook and nearby player obstruction, but do not
	// invent future actions or replay shots from the last received input.
	for(int Type = 0; Type < CGameWorld::NUM_ENTTYPES; ++Type)
	{
		for(CEntity *pEntity = World.FindFirst(Type); pEntity;)
		{
			CEntity *pNext = pEntity->TypeNext();
			if(Type != CGameWorld::ENTTYPE_CHARACTER)
				delete pEntity;
			else
			{
				auto *pCharacter = static_cast<CCharacter *>(pEntity);
				CNetObj_PlayerInput Input = {};
				Input.m_TargetY = -1;
				pCharacter->SetInput(&Input);
				pCharacter->OnPredictedInput(&Input);
				pCharacter->ResetHook();
			}
			pEntity = pNext;
		}
	}
}
}

// Every node is reached by an actual sequence of walking/jumping inputs.
// An empty-space flood fill would incorrectly certify isolated platforms.
class CRescueNavigation
{
	struct CNode
	{
		vec2 m_Position;
		vec2 m_Velocity;
		int m_Jumped;
		int m_JumpedTotal;
		int m_Parent;
		int m_Ticks;
		bool m_Grounded;
		bool m_Standing;
		std::array<vec2, NAVIGATION_ACTION_TICKS> m_aTravel;
	};
	CRescueSolver &m_Solver;
	CCharacterCore m_BaseCore;
	CCharacterCore m_WorkCore;
	CWorldCore m_World;
	CTeamsCore m_Teams;
	std::vector<CNode> m_vNodes;
	std::set<std::array<int, 7>> m_Visited;
	std::vector<int> m_vAirNodes;
	size_t m_Head = 0;
	int m_Action = 0;
	size_t m_AirIndex = 0;
	bool m_InitialPending = true;
	bool m_Jumping = false;
	size_t m_WalkingNodes = 0;

	static bool SwitchActive(unsigned char Number, void *pUser)
	{
		auto &This = *static_cast<CRescueNavigation *>(pUser);
		const auto &Switchers = This.m_World.m_vSwitchers;
		return Number < Switchers.size() && This.m_Solver.m_Team >= 0 && This.m_Solver.m_Team < NUM_DDRACE_TEAMS &&
			Switchers[Number].m_aStatus[This.m_Solver.m_Team];
	}

	int Add(const CCharacterCore &Core, int Parent, int Ticks, const std::array<vec2, NAVIGATION_ACTION_TICKS> &Travel)
	{
		const bool Grounded = (m_Solver.Collision()->IsOnGround(Core.m_Pos, CCharacterCore::PhysicalSize()) ||
			(m_Solver.Collision()->GetMoveRestrictions(SwitchActive, this, Core.m_Pos + vec2(0, 18), 0) & CANTMOVE_DOWN)) && std::abs(Core.m_Vel.y) < 0.1f;
		const bool Standing = Grounded && std::abs(Core.m_Vel.x) < 0.25f;
		const std::array<int, 7> Key = {(int)std::floor(Core.m_Pos.x / 8), (int)std::floor(Core.m_Pos.y / 4),
			round_to_int(Core.m_Vel.x / 2), round_to_int(Core.m_Vel.y / 2), Core.m_Jumped & 3, Core.m_JumpedTotal, Standing ? 1 : 0};
		if(m_vNodes.size() >= MAX_NAVIGATION_STATES || !m_Visited.insert(Key).second)
			return -1;
		const int Index = m_vNodes.size();
		m_vNodes.push_back({Core.m_Pos, Core.m_Vel, Core.m_Jumped, Core.m_JumpedTotal, Parent, Ticks, Grounded, Standing, Travel});
		if(!Grounded && distance(Core.m_Pos, m_Solver.m_TargetPos) <= m_Solver.m_Range)
			m_vAirNodes.push_back(Index);
		return Index;
	}

public:
	explicit CRescueNavigation(CRescueSolver &Solver) : m_Solver(Solver)
	{
		m_BaseCore = Solver.m_pSnapshot->GetCharacterById(Solver.m_LocalId)->GetCore();
		m_Teams = *Solver.m_pSnapshot->Teams();
		m_World.m_vSwitchers = Solver.m_pSnapshot->Switchers();
		m_vNodes.reserve(MAX_NAVIGATION_STATES);
		std::array<vec2, NAVIGATION_ACTION_TICKS> Travel;
		Travel.fill(m_BaseCore.m_Pos);
		Add(m_BaseCore, -1, 0, Travel);
	}
	void Load(CCharacterCore &Core, int Node) const
	{
		const auto &State = m_vNodes[Node];
		Core.m_Pos = State.m_Position;
		Core.m_Vel = State.m_Velocity;
		Core.m_Jumped = State.m_Jumped;
		Core.m_JumpedTotal = State.m_JumpedTotal;
	}
	vec2 Position(int Node) const { return m_vNodes[Node].m_Position; }
	bool Grounded(int Node) const { return m_vNodes[Node].m_Grounded; }
	bool Complete()
	{
		if(m_Head >= m_vNodes.size() && !m_Jumping && m_vNodes.size() < MAX_NAVIGATION_STATES)
		{
			m_Jumping = true;
			m_WalkingNodes = m_vNodes.size();
			m_Head = 0;
			m_Action = 3;
		}
		return m_Head >= m_vNodes.size() || m_vNodes.size() >= MAX_NAVIGATION_STATES;
	}
	int Restrictions(vec2 Position, float Distance = 18.0f) { return m_Solver.Collision()->GetMoveRestrictions(SwitchActive, this, Position, Distance); }
	int Progress() const { return std::min(69, (m_Jumping ? 35 : 0) + (int)m_Head * 35 / MAX_NAVIGATION_STATES); }
	int AirProgress() const { return 70 + m_AirIndex * 30 / std::max<size_t>(1, m_vAirNodes.size()); }
	std::vector<vec2> Approach(int Node) const
	{
		std::vector<vec2> vPoints;
		for(int Index = Node; Index >= 0; Index = m_vNodes[Index].m_Parent)
		{
			if(m_vNodes[Index].m_Parent < 0) vPoints.push_back(m_vNodes[Index].m_Position);
			else for(int Tick = NAVIGATION_ACTION_TICKS - 1; Tick >= 0; --Tick) vPoints.push_back(m_vNodes[Index].m_aTravel[Tick]);
		}
		std::reverse(vPoints.begin(), vPoints.end());
		return vPoints;
	}
	void PrepareAir()
	{
		std::stable_sort(m_vAirNodes.begin(), m_vAirNodes.end(), [this](int Left, int Right) {
			const auto Cost = [this](int Node) { return m_vNodes[Node].m_Ticks + distance(Position(Node), m_Solver.m_LocalPos) * 0.01f; };
			return Cost(Left) < Cost(Right);
		});
	}
	int NextAir() { return m_AirIndex < m_vAirNodes.size() ? m_vAirNodes[m_AirIndex++] : -1; }

	// Return a newly reached stable ground position; air nodes are saved for
	// a separate pass only after every ground branch has been exhausted.
	int Step()
	{
		if(m_InitialPending)
		{
			m_InitialPending = false;
			if(m_vNodes[0].m_Standing) return 0;
		}
		if(Complete()) return -1;
		const int Parent = m_Head;
		if(m_Jumping && m_Head < m_WalkingNodes && m_Action == 0) m_Action = 3;
		const int Action = m_Action;
		if(++m_Action == (m_Jumping ? 6 : 3)) { m_Action = 0; ++m_Head; }
		if(m_vNodes[Parent].m_Ticks >= MAX_NAVIGATION_TICKS) return -1;
		m_WorkCore = m_BaseCore;
		Load(m_WorkCore, Parent);
		m_WorkCore.SetCoreWorld(&m_World, m_Solver.Collision(), &m_Teams);
		m_WorkCore.SetAntiPingInterfereCallback([](int, bool) {});
		m_World.m_apCharacters[m_Solver.m_LocalId] = &m_WorkCore;
		m_WorkCore.m_Input = {};
		m_WorkCore.m_Input.m_TargetY = -1;
		const int aDirections[] = {0, -1, 1};
		m_WorkCore.m_Input.m_Direction = aDirections[Action % 3];
		m_WorkCore.m_Input.m_Jump = Action >= 3;
		std::array<vec2, NAVIGATION_ACTION_TICKS> Travel;
		for(int Tick = 0; Tick < NAVIGATION_ACTION_TICKS; ++Tick)
		{
			int Zone = 0;
			if(m_Solver.m_pSnapshot->m_WorldConfig.m_UseTuneZones)
			{
				Zone = m_Solver.Collision()->IsTune(m_Solver.Collision()->GetPureMapIndex(m_WorkCore.m_Pos));
				if(!Zone) Zone = m_Solver.Collision()->GetAxiomQuadTuneZone(m_WorkCore.m_Pos);
			}
			const auto *pLocal = m_Solver.m_pSnapshot->GetCharacterById(m_Solver.m_LocalId);
			if(pLocal->GetOverriddenTuneZone() != pLocal->GetPureTuneZone()) Zone = pLocal->GetOverriddenTuneZone();
			m_WorkCore.m_Tuning = *m_Solver.m_pSnapshot->GetTuning(Zone);
			const vec2 From = m_WorkCore.m_Pos;
			m_WorkCore.Tick(true);
			// Match the character's extra jump and stopper rules before Move().
			if(m_WorkCore.m_Jumps == -1 || m_WorkCore.m_Jumps == 0 || (m_WorkCore.m_Jumps == 1 && m_WorkCore.m_Jumped > 0)) m_WorkCore.m_Jumped |= 2;
			else if(m_WorkCore.m_JumpedTotal < m_WorkCore.m_Jumps - 1 && m_WorkCore.m_Jumped > 1) m_WorkCore.m_Jumped = 1;
			if((m_WorkCore.m_Super || m_WorkCore.m_EndlessJump) && m_WorkCore.m_Jumped > 1) m_WorkCore.m_Jumped = 1;
			const int Restrictions = m_Solver.Collision()->GetMoveRestrictions(SwitchActive, this, From);
			if(m_WorkCore.m_Vel.y > 0 && (Restrictions & CANTMOVE_DOWN)) { m_WorkCore.m_Jumped = 0; m_WorkCore.m_JumpedTotal = 0; }
			m_WorkCore.m_Vel = ClampVel(Restrictions, m_WorkCore.m_Vel);
			if(length(m_WorkCore.m_Vel) > 64) return -1;
			m_WorkCore.Move();
			m_WorkCore.Quantize();
			if(!m_Solver.NavigationSegment(From, m_WorkCore.m_Pos)) return -1;
			Travel[Tick] = m_WorkCore.m_Pos;
		}
		const int Child = Add(m_WorkCore, Parent, m_vNodes[Parent].m_Ticks + NAVIGATION_ACTION_TICKS, Travel);
		return Child >= 0 && m_vNodes[Child].m_Standing ? Child : -1;
	}
};

CRescueSolver::CRescueSolver() { OnReset(); }
CRescueSolver::~CRescueSolver() = default;

void CRescueSolver::Clear()
{
	m_Stage = EStage::IDLE;
	m_Result = {};
	m_vCandidates.clear();
	m_pSimulation.reset();
	m_pNavigation.reset();
	m_pSnapshot.reset();
	str_copy(m_aStatus, "选择被冻结的队友后点击求解");
}

void CRescueSolver::OnReset()
{
	Clear();
	m_Target = -1;
	m_LocalId = -1;
}

void CRescueSolver::OnStateChange(int NewState, int OldState) { OnReset(); }

bool CRescueSolver::Frozen(const CCharacter *pCharacter) const
{
	return pCharacter && (pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen ||
		pCharacter->Core()->m_LiveFrozen || pCharacter->Core()->m_IsInFreeze ||
		pCharacter->Core()->m_FreezeEnd > Client()->GameTick(g_Config.m_ClDummy));
}

bool CRescueSolver::IsCandidate(int ClientId) const
{
	if(Client()->State() != IClient::STATE_ONLINE || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const int Local = GameClient()->m_Snap.m_LocalClientId;
	if(Local < 0 || Local == ClientId || GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;
	const auto &Data = GameClient()->m_aClients[ClientId];
	const auto &Self = GameClient()->m_aClients[Local];
	return Data.m_Active && GameClient()->m_Snap.m_aCharacters[ClientId].m_Active &&
		GameClient()->m_Snap.m_aCharacters[Local].m_Active && !Data.m_Solo && !Self.m_Solo &&
		GameClient()->m_Teams.SameTeam(Local, ClientId) && GameClient()->m_Teams.CanCollide(Local, ClientId) &&
		Frozen(GameClient()->m_GameWorld.GetCharacterById(ClientId));
}

void CRescueSolver::SelectTarget(int ClientId)
{
	if(m_Target == ClientId)
		return;
	Clear();
	m_Target = ClientId;
}

bool CRescueSolver::UnsupportedTile(vec2 Position) const
{
	const int Index = Collision()->GetPureMapIndex(Position);
	return Collision()->IsTeleport(Index) || Collision()->IsEvilTeleport(Index) ||
		Collision()->IsCheckTeleport(Index) || Collision()->IsCheckEvilTeleport(Index) ||
		Collision()->HasAxiomQuadTile(Position, TILE_TELECHECKINEVIL);
}

bool CRescueSolver::FreezeTile(int Index, CGameWorld &World) const
{
	const auto Freezing = [](int Tile) {
		return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
	};
	if(Freezing(Collision()->GetTileIndex(Index)) || Freezing(Collision()->GetFrontTileIndex(Index)))
		return true;
	const int Number = Collision()->GetSwitchNumber(Index);
	const auto &Switchers = World.Switchers();
	const bool Active = Number == 0 || (Number > 0 && Number < (int)Switchers.size() &&
		m_Team >= 0 && m_Team < NUM_DDRACE_TEAMS && Switchers[Number].m_aStatus[m_Team]);
	return Active && Freezing(Collision()->GetSwitchType(Index));
}

bool CRescueSolver::Hazard(vec2 Position, CGameWorld &World, bool IncludeFreeze) const
{
	const float HalfSize = CCharacterCore::PhysicalSize() / 2;
	if(Position.x < HalfSize || Position.y < HalfSize || Position.x >= Collision()->GetWidth() * 32 - HalfSize || Position.y >= Collision()->GetHeight() * 32 - HalfSize)
		return true;
	// HandleTiles freezes the centre, not the 28x28 wall-collision box.
	// A tee may overlap the water with its body while its centre stays dry.
	if(IncludeFreeze && FreezeTile(Collision()->GetPureMapIndex(Position), World))
		return true;
	for(int Tile : Collision()->GetAxiomQuadTiles(Position))
		if(Tile == TILE_DEATH || (IncludeFreeze && (Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE)))
			return true;
	// Match HandleSkippableTiles' four death probes, independently of freeze.
	const float DeathOffset = CCharacterCore::PhysicalSize() / 3;
	for(int y : {-1, 1})
		for(int x : {-1, 1})
		{
			const vec2 Point = Position + vec2(x * DeathOffset, y * DeathOffset);
			if(Collision()->GetCollisionAt(Point.x, Point.y) == TILE_DEATH || Collision()->GetFrontCollisionAt(Point.x, Point.y) == TILE_DEATH)
				return true;
		}
	return false;
}

bool CRescueSolver::SafePosition(vec2 Position, CGameWorld &World) const
{
	return !Collision()->TestBox(Position, CCharacterCore::PhysicalSizeVec2()) &&
		!Hazard(Position, World, true) && !UnsupportedTile(Position);
}

bool CRescueSolver::NavigationSegment(vec2 From, vec2 To) const
{
	const float MaximumDistance = std::min(2048.0f, std::max(512.0f, distance(m_LocalPos, m_TargetPos) + m_Range));
	if(distance(To, m_LocalPos) > MaximumDistance) return false;
	// Follow the centre's anti-skip tile path, just like DDRacePostCoreTick.
	for(int Index : Collision()->GetMapIndices(From, To))
		if(FreezeTile(Index, *m_pSnapshot)) return false;
	const auto UnsupportedMovement = [](int Tile) {
		return Tile == TILE_JUMP || Tile == TILE_WALLJUMP || Tile == TILE_REFILL_JUMPS ||
			Tile == TILE_UNLIMITED_JUMPS_ENABLE || Tile == TILE_UNLIMITED_JUMPS_DISABLE ||
			Tile == TILE_SOLO_ENABLE || Tile == TILE_SOLO_DISABLE || Tile == TILE_NPC_ENABLE ||
			Tile == TILE_NPC_DISABLE || Tile == TILE_NPH_ENABLE || Tile == TILE_NPH_DISABLE;
	};
	// Reachability comes from the simulated walking/jumping route, regardless
	// of which geometric side of the teammate it reaches. Sweep the wall box
	// and centre hazards separately along that route.
	const int Steps = std::max(1, (int)std::ceil(distance(From, To) / 4));
	const vec2 Delta = To - From;
	for(int Step = 0; Step <= Steps; ++Step)
	{
		const vec2 Position = mix(From, To, (float)Step / Steps);
		if(!SafePosition(Position, *m_pSnapshot)) return false;
		const int Restrictions = m_pNavigation->Restrictions(Position);
		if((Delta.x > 0.01f && (Restrictions & CANTMOVE_RIGHT)) || (Delta.x < -0.01f && (Restrictions & CANTMOVE_LEFT)) ||
			(Delta.y > 0.01f && (Restrictions & CANTMOVE_DOWN)) || (Delta.y < -0.01f && (Restrictions & CANTMOVE_UP))) return false;
		const int Index = Collision()->GetPureMapIndex(Position);
		const int Switch = Collision()->GetSwitchType(Index);
		if(Collision()->IsSpeedup(Index) || UnsupportedMovement(Collision()->GetTileIndex(Index)) ||
			UnsupportedMovement(Collision()->GetFrontTileIndex(Index)) || UnsupportedMovement(Switch) ||
			Switch == TILE_SWITCHOPEN || Switch == TILE_SWITCHCLOSE || Switch == TILE_SWITCHTIMEDOPEN || Switch == TILE_SWITCHTIMEDCLOSE)
			return false;
		for(int Tile : Collision()->GetAxiomQuadTiles(Position))
			if(UnsupportedMovement(Tile)) return false;
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		{
			const auto *pOther = m_pSnapshot->GetCharacterById(Id);
			if(Id != m_LocalId && pOther && m_pSnapshot->Teams()->CanCollide(m_LocalId, Id) &&
				!pOther->Core()->m_CollisionDisabled && !pOther->Core()->m_Solo && distance(Position, pOther->Core()->m_Pos) < 35)
				return false;
		}
	}
	return true;
}

void CRescueSolver::Start()
{
	Clear();
	if(!g_Config.m_XcRescueSolver)
	{
		str_copy(m_aStatus, "请先启用钩救求解");
		return;
	}
	if(!IsCandidate(m_Target))
	{
		str_copy(m_aStatus, "请选择同队、当前可见的冻结队友");
		return;
	}
	m_LocalId = GameClient()->m_Snap.m_LocalClientId;
	auto &World = GameClient()->m_GameWorld;
	CCharacter *pLocal = World.GetCharacterById(m_LocalId);
	if(!World.m_WorldConfig.m_IsDDRace || !pLocal)
	{
		str_copy(m_aStatus, "需要在 DDRace / Gores 游戏中求解");
		return;
	}
	if(Frozen(pLocal) || pLocal->Core()->m_HookHitDisabled || !pLocal->Core()->m_Tuning.m_PlayerHooking)
	{
		str_copy(m_aStatus, "自己被冻结或当前无法钩住玩家");
		return;
	}
	if(length(pLocal->Core()->m_Vel) > 64 || (pLocal->Core()->m_ActiveWeapon == WEAPON_NINJA && pLocal->Core()->m_Ninja.m_CurrentMoveTime > 0))
	{
		str_copy(m_aStatus, "请在移动稳定后重新求解");
		return;
	}
	m_Dummy = g_Config.m_ClDummy;
	m_Team = GameClient()->m_Teams.Team(m_LocalId);
	m_StartTick = World.GameTick();
	m_StartedAt = time_get();
	m_LocalPos = m_LastLocalPos = pLocal->Core()->m_Pos;
	m_TargetPos = World.GetCharacterById(m_Target)->Core()->m_Pos;
	str_copy(m_aTargetName, GameClient()->m_aClients[m_Target].m_aName);
	// Search at most 32 tiles around the target, regardless of custom hook tunes.
	m_Range = std::clamp((float)pLocal->Core()->m_Tuning.m_HookLength, 32.0f, 1024.0f);
	m_SearchingAir = false;
	m_pSnapshot = std::make_unique<CGameWorld>();
	m_pSnapshot->CopyWorldClean(&World);
	m_pSnapshot->m_LocalClientId = m_LocalId;
	Neutralize(*m_pSnapshot);
	m_pSimulation = std::make_unique<CGameWorld>();
	{
		CCollisionTimeScope TimeScope(Collision());
		Collision()->SetAxiomQuadTick(m_StartTick, SERVER_TICK_SPEED);
		m_QuadTime = Collision()->AxiomQuadTime();
		m_pNavigation = std::make_unique<CRescueNavigation>(*this);
	}
	m_Stage = EStage::NAVIGATE;
	str_copy(m_aStatus, "正在搜索行走连通区域内的地面站位…");
}

bool CRescueSolver::HookRay(vec2 Position, vec2 Direction, float HookLength, float HookSpeed) const
{
	if(HookLength <= 0 || HookSpeed <= 0)
		return false;
	vec2 From = Position + Direction * CCharacterCore::PhysicalSize() * 1.5f;
	// Match core flight segments, including the launch offset and player-before-wall rule.
	for(int Tick = 0; Tick < 64; ++Tick)
	{
		vec2 To = From + Direction * HookSpeed;
		const bool Retract = distance(Position, To) > HookLength;
		if(Retract)
			To = Position + Direction * HookLength;
		int Tele = 0;
		const int Hit = Collision()->IntersectLineTeleHook(From, To, &To, nullptr, &Tele);
		int FirstPlayer = -1;
		float FirstDistance = 1e20f;
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		{
			const CCharacter *pOther = m_pSnapshot->GetCharacterById(Id);
			if(!pOther || Id == m_LocalId || pOther->Core()->m_Solo ||
				!m_pSnapshot->Teams()->CanCollide(m_LocalId, Id))
				continue;
			vec2 Closest;
			const vec2 OtherPos = pOther->Core()->m_Pos;
			// The core chooses the nearest player centre in this segment.
			if(closest_point_on_line(From, To, OtherPos, Closest) &&
				distance(OtherPos, Closest) < (Id == m_Target ? 29.5f : 30.0f) && distance(From, OtherPos) < FirstDistance)
			{
				FirstPlayer = Id;
				FirstDistance = distance(From, OtherPos);
			}
		}
		if(FirstPlayer >= 0)
			return FirstPlayer == m_Target;
		// Uncertain/random hook teleports are deliberately not proposed.
		if(Hit || Tele || Retract)
			return false;
		From = To;
	}
	return false;
}

bool CRescueSolver::SearchPosition(int NavigationNode)
{
	const vec2 Position = m_pNavigation->Position(NavigationNode);
	const float Distance = distance(Position, m_TargetPos);
	if(Distance < 56 || Distance > m_Range || !SafePosition(Position, *m_pSnapshot))
		return false;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		const CCharacter *pOther = m_pSnapshot->GetCharacterById(Id);
		if(Id != m_LocalId && pOther && m_pSnapshot->Teams()->CanCollide(m_LocalId, Id) && distance(Position, pOther->Core()->m_Pos) < 36)
			return false;
	}
	int Zone = m_pSnapshot->m_WorldConfig.m_UseTuneZones ? Collision()->IsTune(Collision()->GetPureMapIndex(Position)) : 0;
	if(Zone == 0 && m_pSnapshot->m_WorldConfig.m_UseTuneZones)
		Zone = Collision()->GetAxiomQuadTuneZone(Position);
	const auto *pTuning = m_pSnapshot->GetTuning(Zone);
	if(!pTuning->m_PlayerHooking || Distance > (float)pTuning->m_HookLength + 28)
		return false;
	const bool Grounded = m_pNavigation->Grounded(NavigationNode);
	const float BaseAngle = angle(m_TargetPos - Position);
	const float Cone = std::asin(std::min(29.0f / Distance, 0.99f));
	// Sweep from the centre toward both tangents, including narrow edge hooks.
	for(int AimIndex = 0; AimIndex <= 64; ++AimIndex)
	{
		const float Offset = ((AimIndex + 1) / 2) * (0.985f / 32.0f) * (AimIndex % 2 ? -1 : 1);
		vec2 Aim = direction(BaseAngle + Cone * Offset);
		// Quantize to the same integer vector displayed in settings.
		Aim = normalize(vec2(round_to_int(Aim.x * 1000), round_to_int(Aim.y * 1000)));
		if(!HookRay(Position, Aim, pTuning->m_HookLength, pTuning->m_HookFireSpeed))
			continue;
		m_vCandidates.assign(1, CCandidate{Position, Aim, Grounded, NavigationNode});
		return true;
	}
	return false;
}

void CRescueSolver::BeginSimulation()
{
	m_pSimulation->CopyWorldClean(m_pSnapshot.get());
	m_pSimulation->m_LocalClientId = m_LocalId;
	auto *pLocal = m_pSimulation->GetCharacterById(m_LocalId);
	auto Core = pLocal->GetCore();
	m_pNavigation->Load(Core, m_vCandidates.front().m_NavigationNode);
	Core.m_HookPos = Core.m_Pos;
	Core.m_HookState = HOOK_IDLE;
	Core.m_NewHook = false;
	pLocal->SetCore(Core);
	pLocal->m_Pos = pLocal->m_PrevPos = pLocal->m_PrevPrevPos = Core.m_Pos;
	CNetObj_PlayerInput Input = {};
	Input.m_Hook = 1;
	Input.m_TargetX = round_to_int(m_vCandidates.front().m_Direction.x * 1000);
	Input.m_TargetY = round_to_int(m_vCandidates.front().m_Direction.y * 1000);
	pLocal->SetInput(&Input);
	pLocal->OnPredictedInput(&Input);
	m_SimTick = 0;
}

bool CRescueSolver::SimulateTick()
{
	auto &World = *m_pSimulation;
	auto *pLocal = World.GetCharacterById(m_LocalId);
	auto *pTarget = World.GetCharacterById(m_Target);
	if(!pLocal || !pTarget)
		return false;
	// Reject teleport routes before they can consume a random exit.
	if(UnsupportedTile(pLocal->Core()->m_Pos) || UnsupportedTile(pTarget->Core()->m_Pos))
		return false;
	++World.m_GameTick;
	World.Tick();
	++m_SimTick;
	pLocal = World.GetCharacterById(m_LocalId);
	pTarget = World.GetCharacterById(m_Target);
	if(!pLocal || !pTarget)
		return false;
	const vec2 Local = pLocal->Core()->m_Pos;
	const vec2 Target = pTarget->Core()->m_Pos;
	if(Hazard(Local, World, true) || Frozen(pLocal) || Hazard(Target, World, false) ||
		UnsupportedTile(Local) || UnsupportedTile(Target))
		return false;
	if(pLocal->Core()->HookedPlayer() == m_Target)
	{
		// Stop at attachment. Pull direction, unfreezing and later movement are
		// deliberately outside this tool's objective.
		FinishCandidate();
		Finish();
		return false;
	}
	if(m_vCandidates.front().m_Grounded && !pLocal->IsGrounded()) return false;
	if(m_SimTick >= MAX_FLIGHT_TICKS || pLocal->Core()->m_HookState != HOOK_FLYING)
		return false;
	return true;
}

void CRescueSolver::FinishCandidate()
{
	const auto &Candidate = m_vCandidates.front();
	m_Result.m_Valid = true;
	m_Result.m_Grounded = Candidate.m_Grounded;
	m_Result.m_Position = Candidate.m_Position;
	m_Result.m_Direction = Candidate.m_Direction;
	m_Result.m_MouseX = round_to_int(Candidate.m_Direction.x * 1000);
	m_Result.m_MouseY = round_to_int(Candidate.m_Direction.y * 1000);
	m_Result.m_Angle = std::fmod(angle(Candidate.m_Direction) * 180.0f / pi + 360.0f, 360.0f);
	m_Result.m_vApproach = m_pNavigation->Approach(Candidate.m_NavigationNode);
}

void CRescueSolver::Finish()
{
	m_Stage = EStage::IDLE;
	m_pSimulation.reset();
	if(!m_Result.m_Valid)
		str_copy(m_aStatus, "未找到可到达的起钩位置");
	else
		str_copy(m_aStatus, m_Result.m_Grounded ? "找到可到达的地面起钩位置，钩中目标后清除" : "推荐可到达的空中起钩位置，钩中目标后清除");
}

void CRescueSolver::Invalidate(const char *pReason)
{
	Clear();
	str_copy(m_aStatus, pReason);
}

bool CRescueSolver::Validate()
{
	if(!m_pSnapshot)
		return false;
	if(!g_Config.m_XcRescueSolver)
	{
		Invalidate("钩救求解已关闭");
		return false;
	}
	// A completed advisory stays pinned until an actual server-confirmed
	// attachment (OnNewSnapshot), explicit clearing, or a session/map reset.
	// Snapshot drift, thawing, player movement and elapsed time cannot erase it.
	if(m_Result.m_Valid)
		return true;
	const auto *pLocal = GameClient()->m_GameWorld.GetCharacterById(m_LocalId);
	const auto *pTarget = GameClient()->m_GameWorld.GetCharacterById(m_Target);
	if(m_Dummy != g_Config.m_ClDummy || m_LocalId != GameClient()->m_Snap.m_LocalClientId ||
		!IsCandidate(m_Target) || !pLocal || !pTarget || GameClient()->m_Teams.Team(m_LocalId) != m_Team ||
		str_comp(m_aTargetName, GameClient()->m_aClients[m_Target].m_aName) != 0)
	{
		Invalidate("角色或队伍状态已变化，请重新选择并求解");
		return false;
	}
	if(distance(pTarget->Core()->m_Pos, m_TargetPos) > 8 || Frozen(pLocal) ||
		distance(pLocal->Core()->m_Pos, m_LastLocalPos) > 128 || time_get() - m_StartedAt > time_freq() * 60)
	{
		Invalidate("现场已变化或结果已过期，请重新求解");
		return false;
	}
	m_LastLocalPos = pLocal->Core()->m_Pos;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		if(Id == m_LocalId || Id == m_Target)
			continue;
		const auto *pBefore = m_pSnapshot->GetCharacterById(Id);
		const auto *pNow = GameClient()->m_GameWorld.GetCharacterById(Id);
		const bool NearBefore = pBefore && distance(pBefore->Core()->m_Pos, m_TargetPos) < m_Range + 64;
		const bool NearNow = pNow && distance(pNow->Core()->m_Pos, m_TargetPos) < m_Range + 64;
		if((NearBefore || NearNow) && GameClient()->m_Teams.CanCollide(m_LocalId, Id) &&
			(!pBefore || !pNow || distance(pBefore->Core()->m_Pos, pNow->Core()->m_Pos) > 24))
		{
			Invalidate("附近玩家发生变化，请重新求解");
			return false;
		}
	}
	return true;
}

void CRescueSolver::OnNewSnapshot()
{
	if(!Validate())
		return;
	if(m_Result.m_Valid && m_Dummy == g_Config.m_ClDummy)
	{
		// Read the received object, not the locally evolved/predicted core.
		const auto *pCharacter = static_cast<const CNetObj_Character *>(Client()->SnapFindItem(IClient::SNAP_CURRENT, NETOBJTYPE_CHARACTER, m_LocalId));
		if(pCharacter && pCharacter->m_HookState == HOOK_GRABBED && pCharacter->m_HookedPlayer == m_Target)
			Invalidate("已钩中救援目标，标记已清除");
	}
}

int CRescueSolver::Progress() const
{
	if(m_pNavigation && Busy())
		return m_SearchingAir ? m_pNavigation->AirProgress() : m_pNavigation->Progress();
	return 100;
}

void CRescueSolver::OnUpdate()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	if(!Validate() || !Busy())
		return;
	CCollisionTimeScope TimeScope(Collision());
	const int64_t Deadline = time_get() + std::max<int64_t>(1, time_freq() / 500); // ~2 ms per frame
	// Also bound work units for low-resolution clocks and unusually fast frames.
	int Units = 0;
	while(Busy() && time_get() < Deadline && ++Units <= 64)
	{
		if(m_Stage == EStage::NAVIGATE)
		{
			Collision()->SetAxiomQuadTime(m_QuadTime);
			if(m_pNavigation->Complete())
			{
				m_SearchingAir = true;
				m_pNavigation->PrepareAir();
				m_Stage = EStage::AIR_SEARCH;
				str_copy(m_aStatus, "地面无可行解，正在检查可到达的空中位置…");
				continue;
			}
			const int Node = m_pNavigation->Step();
			if(Node >= 0 && SearchPosition(Node))
			{
				m_Stage = EStage::SIMULATE;
				BeginSimulation();
			}
		}
		else if(m_Stage == EStage::AIR_SEARCH)
		{
			Collision()->SetAxiomQuadTime(m_QuadTime);
			const int Node = m_pNavigation->NextAir();
			if(Node < 0) Finish();
			else if(SearchPosition(Node)) { m_Stage = EStage::SIMULATE; BeginSimulation(); }
		}
		else if(m_Stage == EStage::SIMULATE)
		{
			if(!SimulateTick())
			{
				if(!Busy()) break;
				m_Stage = m_SearchingAir ? EStage::AIR_SEARCH : EStage::NAVIGATE;
			}
		}
	}
}

void CRescueSolver::OnRender()
{
	if(!g_Config.m_XcRescueSolver || !g_Config.m_XcRescueOverlay || Busy() || !m_Result.m_Valid ||
		Client()->State() != IClient::STATE_ONLINE || GameClient()->IsRenderingDummyMiniMap() || !Validate())
		return;
	const CScreenRect PreviousScreen = Graphics()->GetScreen();
	Graphics()->MapScreen(Graphics()->MapScreenToWorld(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y,
		100, 100, 100, 0, 0, Graphics()->GameScreenAspect(), GameClient()->m_Camera.m_Zoom));
	const vec2 Position = m_Result.m_Position;
	const ColorRGBA Color(0.3f, 1, 0.65f, 0.9f);
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(Color);
	const IGraphics::CLineItem aMarker[] = {
		{Position.x - 18, Position.y, Position.x + 18, Position.y},
		{Position.x, Position.y - 18, Position.x, Position.y + 18},
		{Position.x - 14, Position.y - 14, Position.x + 14, Position.y - 14},
		{Position.x + 14, Position.y - 14, Position.x + 14, Position.y + 14},
		{Position.x + 14, Position.y + 14, Position.x - 14, Position.y + 14},
		{Position.x - 14, Position.y + 14, Position.x - 14, Position.y - 14}};
	Graphics()->LinesDraw(aMarker, std::size(aMarker));
	const float HookDistance = distance(Position, m_TargetPos);
	const vec2 AimEnd = Position + m_Result.m_Direction * HookDistance;
	const IGraphics::CLineItem Aim(Position.x, Position.y, AimEnd.x, AimEnd.y);
	Graphics()->LinesDraw(&Aim, 1);
	Graphics()->SetColor(0.4f, 0.7f, 1, 0.55f);
	for(size_t i = 1; i < m_Result.m_vApproach.size(); ++i)
	{
		const vec2 From = m_Result.m_vApproach[i - 1], To = m_Result.m_vApproach[i];
		const IGraphics::CLineItem Segment(From.x, From.y, To.x, To.y);
		Graphics()->LinesDraw(&Segment, 1);
	}
	Graphics()->LinesEnd();
	TextRender()->TextColor(Color);
	char aLabel[192];
	str_format(aLabel, sizeof(aLabel), "钩救站位  X %.1f  Y %.1f  角度 %.2f°", Position.x / 32, Position.y / 32, m_Result.m_Angle);
	TextRender()->Text(Position.x - 80, Position.y - 42, 14, aLabel);
	TextRender()->Text(m_TargetPos.x - 25, m_TargetPos.y - 38, 12, "救援目标");
	TextRender()->TextColor(1, 1, 1, 1);
	Graphics()->SetColor(1, 1, 1, 1);
	Graphics()->MapScreen(PreviousScreen);
}
