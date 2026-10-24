/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CLOUD_INPUT_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CLOUD_INPUT_H

#include <base/vmath.h>
#include <engine/client/enums.h>
#include <generated/protocol.h>
#include <game/client/component.h>

class CControls;
class CGameClient;

class CCloudInput : public CComponent
{
	CNetObj_PlayerInput m_aInput[NUM_DUMMIES]{};
public:
	int Sizeof() const override { return sizeof(*this); }
	bool IsActive() const;
	float Amount() const;
	int SelfTickOffset() const;
	int OthersTickOffset() const;
	CNetObj_PlayerInput &Input(int Dummy) { return m_aInput[Dummy]; }
	const CNetObj_PlayerInput &Input(int Dummy) const { return m_aInput[Dummy]; }
	bool CheckNewInput(const CControls &Controls);
	void ApplyOffset(const CGameClient &GameClient, int ClientId, int &Tick, float &Intra) const;
	bool TryGetPredPos(const CGameClient &GameClient, int ClientId, int Tick, float Intra, vec2 &OutPos) const;
};

#endif
