#ifndef GAME_ENVELOPE_TRIGGER_H
#define GAME_ENVELOPE_TRIGGER_H

#include <engine/shared/protocol.h>

#include <array>
#include <unordered_map>
#include <vector>

class IMap;
class CCollision;
class CTeamsCore;

enum EEnvelopeTriggerType
{
	TRIGGER_TYPE_DEFAULT = 0,
	TRIGGER_TYPE_PAUSE,
	TRIGGER_TYPE_START_ONCE,
	TRIGGER_TYPE_START_LOOP,
	TRIGGER_TYPE_STOP,
	TRIGGER_TYPE_RESET_START_ONCE,
	TRIGGER_TYPE_RESET_START_LOOP,
	NUM_ENVELOPE_TRIGGERS,
};

enum EEnvelopeTriggerBitFlags
{
	TRIGGER_FLAG_NONE = 0,
	TRIGGER_FLAG_TEAM = 1,
};

static constexpr int ENVELOPE_NONE = -1;
static constexpr int ENVELOPE_RESET = -2;

static inline bool IsEnvelopeTriggerPlaying(EEnvelopeTriggerType Type)
{
	return Type != EEnvelopeTriggerType::TRIGGER_TYPE_DEFAULT && Type != EEnvelopeTriggerType::TRIGGER_TYPE_PAUSE && Type != EEnvelopeTriggerType::TRIGGER_TYPE_STOP;
}

class CEnvelopeTrigger
{
public:
	int m_EnvelopeId;
	EEnvelopeTriggerType m_State;
	static const char *ConsoleName(EEnvelopeTriggerType Trigger);
	static EEnvelopeTriggerType FromName(const char *pTriggerName);
};

class CEnvelopeTriggerZone
{
public:
	std::vector<CEnvelopeTrigger> m_vEnvelopeTriggers;
};

// Absolute tick plus a saved playback position makes pause/resume independent
// of rendering, snapshot frequency and prediction rewinds.
struct CEnvelopePlayback
{
	EEnvelopeTriggerType m_Type = TRIGGER_TYPE_DEFAULT;
	int m_StartTick = 0;
	int m_OffsetMillis = 0;
	double PositionMillis(double Tick, int DurationMillis, int MapStartTick) const;
};

class CEnvelopeTriggerRuntime
{
public:
	void Init(IMap *pMap, int MapStartTick);
	bool AddTrigger(int Zone, const char *pType, int Envelope);
	bool SetSpawn(const char *pType);
	void SetTuneZone(int Zone, int EnvelopeZone);
	bool Enabled() const { return m_Enabled; }
	int NumEnvelopes() const { return m_vDurations.size(); }
	int MapStartTick() const { return m_MapStartTick; }
	EEnvelopeTriggerType SpawnType() const { return m_SpawnType; }
	void SetClock(int MapStartTick, EEnvelopeTriggerType SpawnType);
	CEnvelopePlayback State(int Envelope, int ClientId, int Team) const;
	void SetState(int Envelope, int ClientId, const CEnvelopePlayback &State);
	double Time(int Envelope, double Tick, double OffsetSeconds, int ClientId, int Team) const;
	void ResetClient(int ClientId);
	void ResetTeam(int Team, const CTeamsCore &Teams);
	void HandleTile(const CCollision &Collision, int Index, int ClientId, int Team, const CTeamsCore &Teams, int &LastZone, int Tick);

private:
	using CStates = std::unordered_map<int, CEnvelopePlayback>;
	std::unordered_map<int, CEnvelopeTriggerZone> m_Zones;
	std::unordered_map<int, int> m_TuneZones;
	std::unordered_map<int, CStates> m_TeamStates;
	std::array<CStates, MAX_CLIENTS> m_aClientStates;
	std::vector<int> m_vDurations;
	EEnvelopeTriggerType m_SpawnType = TRIGGER_TYPE_DEFAULT;
	int m_MapStartTick = 0;
	bool m_Enabled = false;
};

#endif
