#include "tee_balance_physics.h"

#include <game/collision.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <set>

namespace TeeBalance
{
int WalkingDirection(const CCharacterCore &Core, float StandX)
{
	const float Error = StandX - Core.m_Pos.x;
	// A short input pulse can coast several pixels on low-friction maps.
	// Accept a nearby stand rather than oscillating across an exact point.
	if(std::abs(Error) <= 8.0f) return 0;
	const int Toward = Error > 0 ? 1 : -1;
	if(Core.m_Vel.x * Toward < -0.1f) return 0;
	// With no direction held, friction is applied before each movement.
	// Sum that decay using this map's tuning instead of a fixed multiplier.
	const float Friction = std::clamp((float)Core.m_Tuning.m_GroundFriction, 0.0f, 0.999f);
	const float Coast = std::abs(Core.m_Vel.x) * Friction / (1.0f - Friction);
	return std::abs(Error) > Coast + 2.0f ? Toward : 0;
}

bool InFormation(const CCharacterCore &Lower, const CCharacterCore &Upper, int Direction)
{
	const vec2 Delta = Upper.m_Pos - Lower.m_Pos;
	const float Ahead = Delta.x * Direction;
	return Ahead >= 8 && Ahead <= 36 && Delta.y <= -8 && Delta.y >= -36 &&
		length(Delta) >= 27 && length(Delta) <= 39 &&
		std::abs(Upper.m_Vel.y) < 1.2f && std::abs(Upper.m_Vel.x - Lower.m_Vel.x) < 2.0f;
}

CSimulation::CSimulation(CCollision *pCollision, const CCharacterCore &Lower, const CCharacterCore &Upper,
	const CTeamsCore &Teams, const std::vector<SSwitchers> &Switchers, bool Reverse, bool NoWeak,
	int ReloadTicks, bool PreserveHook) :
	m_pCollision(pCollision), m_Teams(Teams), m_aCores{Lower, Upper}, m_Reverse(Reverse), m_NoWeak(NoWeak), m_ReloadTicks(ReloadTicks)
{
	m_World.m_vSwitchers = Switchers;
	for(auto &Core : m_aCores)
	{
		Core.SetCoreWorld(&m_World, pCollision, &m_Teams);
		Core.SetAntiPingInterfereCallback([](int, bool) {});
		Core.m_AttachedPlayers.clear();
		if(!PreserveHook)
		{
			Core.SetHookedPlayer(-1);
			Core.m_HookState = HOOK_IDLE;
			Core.m_HookPos = Core.m_Pos;
			Core.m_NewHook = false;
		}
		Core.m_Input = {};
		Core.m_Input.m_TargetY = -1;
		m_World.m_apCharacters[Core.m_Id] = &Core;
	}
	if(PreserveHook)
		for(auto &Core : m_aCores)
			if(Core.HookedPlayer() >= 0 && m_World.m_apCharacters[Core.HookedPlayer()])
				m_World.m_apCharacters[Core.HookedPlayer()]->m_AttachedPlayers.insert(Core.m_Id);
}

void CSimulation::Tick(SAction Action)
{
	auto &Lower = m_aCores[0];
	auto &Upper = m_aCores[1];
	const vec2 Delta = Upper.m_Pos - Lower.m_Pos;
	const vec2 RawAim = length(Delta) > 0.01f ? normalize(Delta) : vec2(1, 0);
	const vec2 Aim = normalize(vec2(round_to_int(RawAim.x * 1000), round_to_int(RawAim.y * 1000)));
	Lower.m_Input.m_Direction = Action.m_Direction;
	Lower.m_Input.m_Hook = Action.m_Hook;
	Lower.m_Input.m_Jump = Action.m_Jump;
	Lower.m_Input.m_TargetX = round_to_int(Aim.x * 1000);
	Lower.m_Input.m_TargetY = round_to_int(Aim.y * 1000);
	if(m_ReloadTicks > 0) --m_ReloadTicks;
	// Match DDRace's hammer impulse; a shortlisted recipe is also validated
	// with the complete prediction world before being executed by the client.
	if(Action.m_Hammer && !m_ReloadTicks && !Lower.m_HammerHitDisabled &&
		m_Teams.CanCollide(Lower.m_Id, Upper.m_Id) &&
		distance(Upper.m_Pos, Lower.m_Pos + Aim * 21.0f) < 42.0f)
	{
		const vec2 Boost = normalize(Aim + vec2(0, -1.1f)) * 10.0f;
		const int Restrictions = m_pCollision->GetMoveRestrictions(Upper.m_Pos);
		const vec2 Clamped = ClampVel(Restrictions, Upper.m_Vel + Boost);
		Upper.m_Vel = ClampVel(Restrictions, Upper.m_Vel +
			(vec2(0, -1) + Clamped - Upper.m_Vel) * (float)Lower.m_Tuning.m_HammerStrength);
		m_ReloadTicks = std::max(1, round_to_int((float)Lower.m_Tuning.m_HammerHitFireDelay * SERVER_TICK_SPEED / 1000));
	}
	for(int Order = 0; Order < 2; ++Order) m_aCores[Order ^ m_Reverse].Tick(true, !m_NoWeak);
	if(m_NoWeak)
		for(int Order = 0; Order < 2; ++Order) m_aCores[Order ^ m_Reverse].TickDeferred();
	for(int Order = 0; Order < 2; ++Order)
	{
		auto &Core = m_aCores[Order ^ m_Reverse];
		Core.Move();
		Core.Quantize();
	}
}

void CSimulation::AnchorHorizontal(float LowerX)
{
	// This detached hold probe measures stability over the local support.
	// Preserve relative positions and velocities without travelling hundreds
	// of pixels into unrelated walls during a long stability measurement.
	const float Shift = LowerX - m_aCores[0].m_Pos.x;
	for(auto &Core : m_aCores)
	{
		Core.m_Pos.x += Shift;
		Core.m_HookPos.x += Shift;
	}
}

CSimulation::CSimulation(const CSimulation &Other) :
	m_pCollision(Other.m_pCollision), m_Teams(Other.m_Teams),
	m_aCores{Other.m_aCores[0], Other.m_aCores[1]}, m_Reverse(Other.m_Reverse),
	m_NoWeak(Other.m_NoWeak), m_ReloadTicks(Other.m_ReloadTicks)
{
	m_World.m_vSwitchers = Other.m_World.m_vSwitchers;
	for(auto &Core : m_aCores)
	{
		Core.SetCoreWorld(&m_World, m_pCollision, &m_Teams);
		Core.SetAntiPingInterfereCallback([](int, bool) {});
		m_World.m_apCharacters[Core.m_Id] = &Core;
	}
}

CPlanner::CPlanner(const CSimulation &Initial, int Direction) : m_Direction(Direction)
{
	m_vBeam.push_back({std::make_unique<CSimulation>(Initial), {}, 0, Score(Initial)});
}

float CPlanner::Score(const CSimulation &Simulation) const
{
	const auto &Lower = Simulation.Lower();
	const auto &Upper = Simulation.Upper();
	const vec2 Delta = Upper.m_Pos - Lower.m_Pos;
	return std::abs(Delta.x * m_Direction - 25.0f) + std::abs(Delta.y + 25.0f) +
		std::abs(Upper.m_Vel.x * m_Direction - 3.0f) * 3.0f + std::abs(Upper.m_Vel.y) * 2.0f +
		std::abs(Lower.m_Vel.x * m_Direction - 3.0f);
}

void CPlanner::Step(int WorkBudget)
{
	const auto Less = [](const SNode &A, const SNode &B) {
		return A.m_Score < B.m_Score || (A.m_Score == B.m_Score && A.m_Order < B.m_Order);
	};
	for(int Work = 0; Work < WorkBudget && !m_Finished; ++Work)
	{
		if(m_pProbe)
		{
			m_pProbe->Tick({m_Direction, false, false});
			m_pProbe->AnchorHorizontal(m_ProbeAnchorX);
			if(m_ProbeTick++ >= 50 && !InFormation(m_pProbe->Lower(), m_pProbe->Upper(), m_Direction))
			{
				m_pProbe.reset();
				++m_ProbeIndex;
			}
			else if(m_ProbeTick >= 250)
			{
				m_vResult = m_vBeam[m_ProbeIndex].m_vActions;
				m_vResult.push_back({m_Direction, false, false});
				m_Offset = m_pProbe->Upper().m_Pos - m_pProbe->Lower().m_Pos;
				m_pProbe.reset();
				++m_ProbeIndex;
				m_Finished = true;
			}
			continue;
		}
		if(!m_Expanding)
		{
			if(m_ProbeIndex < std::min<size_t>(8, m_vBeam.size()))
			{
				const auto &Node = m_vBeam[m_ProbeIndex];
				if(Node.m_Score <= 28 && InFormation(Node.m_pSimulation->Lower(), Node.m_pSimulation->Upper(), m_Direction))
				{
					m_pProbe = std::make_unique<CSimulation>(*Node.m_pSimulation);
					m_ProbeAnchorX = m_pProbe->Lower().m_Pos.x;
					m_ProbeTick = 0;
				}
				else ++m_ProbeIndex;
				continue;
			}
			if(m_Depth >= 110 || m_vBeam.empty()) { m_Finished = true; break; }
			m_vNext.clear();
			m_vNext.reserve(160);
			m_Seen.clear();
			m_ExpandIndex = 0;
			m_ActionIndex = 0;
			m_Order = 0;
			m_Expanding = true;
			continue;
		}
		if(m_ExpandIndex >= m_vBeam.size())
		{
			std::sort_heap(m_vNext.begin(), m_vNext.end(), Less);
			m_vBeam = std::move(m_vNext);
			++m_Depth;
			m_ProbeIndex = 0;
			m_Expanding = false;
			continue;
		}
		const auto &Node = m_vBeam[m_ExpandIndex];
		const int Bits = m_ActionIndex++;
		if(m_ActionIndex == 8) { m_ActionIndex = 0; ++m_ExpandIndex; }
		const SAction Action{(Bits & 4) ? 0 : m_Direction, (Bits & 2) != 0, (Bits & 1) != 0};
		if(Action.m_Hammer && (Node.m_Hammers >= 2 || !Node.m_pSimulation->CanHammer() ||
			distance(Node.m_pSimulation->Lower().m_Pos, Node.m_pSimulation->Upper().m_Pos) > 62)) continue;
		SNode Child{std::make_unique<CSimulation>(*Node.m_pSimulation), {}, Node.m_Hammers + (Action.m_Hammer ? 1 : 0), 0, m_Order++};
		Child.m_pSimulation->Tick(Action);
		const auto &Lower = Child.m_pSimulation->Lower();
		const auto &Upper = Child.m_pSimulation->Upper();
		const vec2 Delta = Upper.m_Pos - Lower.m_Pos;
		if(std::abs(Delta.x) > 160 || Delta.y > 16 || Delta.y < -140) continue;
		// Canonical coordinates give left and right the same pruning order.
		const std::array<int, 9> Key{round_to_int(Delta.x * m_Direction), round_to_int(Delta.y),
			round_to_int(Lower.m_Vel.x * m_Direction * 2), round_to_int(Upper.m_Vel.x * m_Direction * 2), round_to_int(Upper.m_Vel.y * 2),
			Lower.m_HookState, Lower.HookedPlayer(), Child.m_Hammers, Lower.m_HookTick / 4};
		if(!m_Seen.insert(Key).second) continue;
		Child.m_Score = Score(*Child.m_pSimulation);
		if(m_vNext.size() >= 160 && !Less(Child, m_vNext.front())) continue;
		Child.m_vActions = Node.m_vActions;
		Child.m_vActions.push_back(Action);
		if(m_vNext.size() >= 160)
		{
			std::pop_heap(m_vNext.begin(), m_vNext.end(), Less);
			m_vNext.pop_back();
		}
		m_vNext.push_back(std::move(Child));
		std::push_heap(m_vNext.begin(), m_vNext.end(), Less);
	}
}
}
