#include "envelope_trigger.h"

#include <base/dbg.h>
#include <base/str.h>

#include <engine/map.h>

#include <game/collision.h>
#include <game/mapitems.h>
#include <game/teamscore.h>

#include <algorithm>
#include <cmath>

static constexpr const char *s_apEnvelopeTriggerNames[NUM_ENVELOPE_TRIGGERS] = {
	"default",
	"pause",
	"start_once",
	"start_loop",
	"stop",
	"reset_start_once",
	"reset_start_loop",
};

const char *CEnvelopeTrigger::ConsoleName(EEnvelopeTriggerType Trigger)
{
	dbg_assert(Trigger >= 0 && Trigger < NUM_ENVELOPE_TRIGGERS, "unknown envelope trigger type");
	return s_apEnvelopeTriggerNames[static_cast<int>(Trigger)];
}

EEnvelopeTriggerType CEnvelopeTrigger::FromName(const char *pTriggerName)
{
	for(int i = 0; i < NUM_ENVELOPE_TRIGGERS; i++)
	{
		if(str_comp(pTriggerName, s_apEnvelopeTriggerNames[i]) == 0)
			return static_cast<EEnvelopeTriggerType>(i);
	}
	return EEnvelopeTriggerType::TRIGGER_TYPE_DEFAULT;
}

double CEnvelopePlayback::PositionMillis(double Tick, int DurationMillis, int MapStartTick) const
{
	if(m_Type == TRIGGER_TYPE_DEFAULT)
		return (Tick - MapStartTick) * 1000.0 / SERVER_TICK_SPEED;
	if(m_Type == TRIGGER_TYPE_STOP)
		return 0;
	double Position = m_OffsetMillis;
	if(m_Type != TRIGGER_TYPE_PAUSE)
		Position += std::max(0.0, Tick - m_StartTick) * 1000.0 / SERVER_TICK_SPEED;
	if(DurationMillis <= 0)
		return 0;
	if(m_Type == TRIGGER_TYPE_START_LOOP || m_Type == TRIGGER_TYPE_RESET_START_LOOP)
		return std::fmod(std::max(0.0, Position), double(DurationMillis));
	return std::clamp(Position, 0.0, double(DurationMillis));
}

void CEnvelopeTriggerRuntime::Init(IMap *pMap, int MapStartTick)
{
	m_Zones.clear();
	m_TuneZones.clear();
	m_TeamStates.clear();
	for(auto &States : m_aClientStates)
		States.clear();
	m_SpawnType = TRIGGER_TYPE_DEFAULT;
	m_MapStartTick = MapStartTick;
	m_Enabled = false;
	int Start, Num, PointStart, PointItems;
	pMap->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
	pMap->GetType(MAPITEMTYPE_ENVPOINTS, &PointStart, &PointItems);
	m_vDurations.assign(Num, 0);
	bool Bezier = false;
	for(int i = 0; i < Num; ++i)
	{
		if(pMap->GetItemSize(Start + i) >= int(sizeof(CMapItemEnvelope_v1)))
			Bezier |= static_cast<const CMapItemEnvelope_v1 *>(pMap->GetItem(Start + i))->m_Version >= CMapItemEnvelope::VERSION_TEEWORLDS_BEZIER;
	}
	if(PointItems <= 0)
		return;
	const void *pPoints = pMap->GetItem(PointStart);
	const int NumPoints = pMap->GetItemSize(PointStart) / (Bezier ? sizeof(CEnvPointBezier_upstream) : sizeof(CEnvPoint));
	for(int i = 0; i < Num; ++i)
	{
		if(pMap->GetItemSize(Start + i) < int(sizeof(CMapItemEnvelope_v1)))
			continue;
		const auto *pEnv = static_cast<const CMapItemEnvelope_v1 *>(pMap->GetItem(Start + i));
		if(pEnv->m_StartPoint < 0 || pEnv->m_StartPoint > NumPoints || pEnv->m_NumPoints <= 0 || pEnv->m_NumPoints > NumPoints - pEnv->m_StartPoint)
			continue;
		const int Last = pEnv->m_StartPoint + pEnv->m_NumPoints - 1;
		m_vDurations[i] = std::max(0, (Bezier ? static_cast<const CEnvPointBezier_upstream *>(pPoints)[Last].m_Time : static_cast<const CEnvPoint *>(pPoints)[Last].m_Time).GetInternal());
	}
}

static bool ValidTriggerName(const char *pType)
{
	for(int i = 0; i < NUM_ENVELOPE_TRIGGERS; ++i)
		if(str_comp(pType, CEnvelopeTrigger::ConsoleName(static_cast<EEnvelopeTriggerType>(i))) == 0)
			return true;
	return false;
}

bool CEnvelopeTriggerRuntime::AddTrigger(int Zone, const char *pType, int Envelope)
{
	if(Zone < 0 || Zone >= 65536 || Envelope < 0 || Envelope >= NumEnvelopes() || !ValidTriggerName(pType))
		return false;
	m_Zones[Zone].m_vEnvelopeTriggers.push_back({Envelope, CEnvelopeTrigger::FromName(pType)});
	m_Enabled = true;
	return true;
}

bool CEnvelopeTriggerRuntime::SetSpawn(const char *pType)
{
	if(!ValidTriggerName(pType))
		return false;
	m_SpawnType = CEnvelopeTrigger::FromName(pType);
	m_Enabled = true;
	return true;
}

void CEnvelopeTriggerRuntime::SetClock(int MapStartTick, EEnvelopeTriggerType SpawnType)
{
	m_MapStartTick = MapStartTick;
	m_SpawnType = SpawnType;
	m_Enabled = true;
}

void CEnvelopeTriggerRuntime::SetTuneZone(int Zone, int EnvelopeZone)
{
	if(Zone >= 0 && Zone < 256 && EnvelopeZone >= 0 && EnvelopeZone < 65536)
		m_TuneZones[Zone] = EnvelopeZone;
}

CEnvelopePlayback CEnvelopeTriggerRuntime::State(int Envelope, int ClientId, int Team) const
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
	{
		const auto It = m_aClientStates[ClientId].find(Envelope);
		if(It != m_aClientStates[ClientId].end())
			return It->second;
	}
	const auto TeamIt = m_TeamStates.find(Team);
	if(TeamIt != m_TeamStates.end())
	{
		const auto It = TeamIt->second.find(Envelope);
		if(It != TeamIt->second.end())
			return It->second;
	}
	return {m_SpawnType, m_MapStartTick, 0};
}

void CEnvelopeTriggerRuntime::SetState(int Envelope, int ClientId, const CEnvelopePlayback &Playback)
{
	if(Envelope >= 0 && Envelope < NumEnvelopes() && ClientId >= 0 && ClientId < MAX_CLIENTS && Playback.m_Type >= 0 && Playback.m_Type < NUM_ENVELOPE_TRIGGERS)
		m_aClientStates[ClientId][Envelope] = Playback;
}

double CEnvelopeTriggerRuntime::Time(int Envelope, double Tick, double OffsetSeconds, int ClientId, int Team) const
{
	if(Envelope < 0 || Envelope >= NumEnvelopes())
		return 0;
	const CEnvelopePlayback Playback = State(Envelope, ClientId, Team);
	const double Duration = m_vDurations[Envelope] / 1000.0;
	double Time = Playback.PositionMillis(Tick, m_vDurations[Envelope], m_MapStartTick) / 1000.0 + OffsetSeconds;
	if(Duration <= 0)
		return 0;
	if(Playback.m_Type == TRIGGER_TYPE_DEFAULT || Playback.m_Type == TRIGGER_TYPE_START_LOOP || Playback.m_Type == TRIGGER_TYPE_RESET_START_LOOP)
	{
		Time = std::fmod(Time, Duration);
		return Time < 0 ? Time + Duration : Time;
	}
	// Both renderers normally wrap at the last point. Stay one microsecond
	// before it so one-shot envelopes hold their final frame instead.
	return std::clamp(Time, 0.0, std::max(0.0, Duration - 0.000001));
}

void CEnvelopeTriggerRuntime::ResetClient(int ClientId)
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
		m_aClientStates[ClientId].clear();
}

void CEnvelopeTriggerRuntime::ResetTeam(int Team, const CTeamsCore &Teams)
{
	m_TeamStates.erase(Team);
	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(Teams.Team(i) == Team)
			ResetClient(i);
}

void CEnvelopeTriggerRuntime::HandleTile(const CCollision &Collision, int Index, int ClientId, int Team, const CTeamsCore &Teams, int &LastZone, int Tick)
{
	if(!m_Enabled || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	int Zone = ENVELOPE_NONE;
	bool Solo = false;
	if(Index >= 0)
	{
		const int SwitchType = Collision.GetSwitchType(Index);
		if(SwitchType == TILE_ENV_TRIGGER_SOLO || SwitchType == TILE_ENV_TRIGGER_TEAM)
		{
			Zone = Collision.GetSwitchNumber(Index) + Collision.GetSwitchDelay(Index) * 256;
			Solo = SwitchType == TILE_ENV_TRIGGER_SOLO;
		}
		else
		{
			const auto It = m_TuneZones.find(Collision.IsTune(Index));
			if(It != m_TuneZones.end())
				Zone = It->second;
		}
	}
	const int ZoneKey = Zone < 0 ? ENVELOPE_NONE : Zone + (Solo ? 65536 : 0);
	if(ZoneKey == LastZone)
		return;
	LastZone = ZoneKey;
	const auto It = m_Zones.find(Zone);
	if(It == m_Zones.end())
		return;
	for(const auto &Trigger : It->second.m_vEnvelopeTriggers)
	{
		const int Env = Trigger.m_EnvelopeId;
		const auto Old = State(Env, ClientId, Team);
		int Offset = 0;
		if(Trigger.m_State == TRIGGER_TYPE_PAUSE || Trigger.m_State == TRIGGER_TYPE_START_ONCE || Trigger.m_State == TRIGGER_TYPE_START_LOOP)
		{
			double Position = Old.PositionMillis(Tick, m_vDurations[Env], m_MapStartTick);
			if(Old.m_Type == TRIGGER_TYPE_DEFAULT && m_vDurations[Env] > 0)
				Position = std::fmod(Position, double(m_vDurations[Env]));
			Offset = std::max(0, int(std::round(Position)));
		}
		const CEnvelopePlayback Playback{Trigger.m_State, Tick, Offset};
		if(Solo)
			m_aClientStates[ClientId][Env] = Playback;
		else
		{
			m_TeamStates[Team][Env] = Playback;
			for(int i = 0; i < MAX_CLIENTS; ++i)
				if(Teams.Team(i) == Team)
					m_aClientStates[i].erase(Env);
		}
	}
}
