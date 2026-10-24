/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_SNAP_TAP_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_SNAP_TAP_H

#include <engine/client.h>
#include <game/client/component.h>

class CSnapTap : public CComponent
{
	int m_aApplied[NUM_DUMMIES]{};
	int m_aLastPressed[NUM_DUMMIES]{};
	int64_t m_aLastPressedTime[NUM_DUMMIES]{};
	int m_aPrevLeft[NUM_DUMMIES]{};
	int m_aPrevRight[NUM_DUMMIES]{};
	void Update(int Dummy, bool Left, bool Right);
	int Resolve(int Dummy, bool Left, bool Right);
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void Reset(int Dummy);
	int ResolveMovementDirection(int Dummy, bool Left, bool Right, bool UpdateState);
	bool IsBlockedByCommunity() const;
};

#endif
