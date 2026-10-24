/* Copyright © 2026 BestProject Team */
#include "cloud_input.h"
#include "inputs.h"

#include <engine/shared/config.h>
#include <game/client/components/controls.h>
#include <game/client/gameclient.h>
#include <cmath>

bool CCloudInput::IsActive() const { return g_Config.m_BcInputs == BC_INPUTS_CLOUD && Amount() > 0.0f; }
float CCloudInput::Amount() const { return g_Config.m_BcCloudInputAmount / 100.0f; }
int CCloudInput::SelfTickOffset() const { return (int)std::ceil(Amount()); }
int CCloudInput::OthersTickOffset() const { return g_Config.m_BcCloudInputOthers && Amount() > 0.0f ? 1 : 0; }

bool CCloudInput::CheckNewInput(const CControls &Controls)
{
	bool Changed[NUM_DUMMIES]{};
	for(int Dummy = 0; Dummy < NUM_DUMMIES; ++Dummy)
	{
		CNetObj_PlayerInput Next = Controls.m_aInputData[Dummy];
		if(Dummy == g_Config.m_ClDummy)
		{
			Next.m_Direction = Controls.m_aInputDirectionLeft[Dummy] == Controls.m_aInputDirectionRight[Dummy] ? 0 : (Controls.m_aInputDirectionLeft[Dummy] ? -1 : 1);
			if(g_Config.m_ClSubTickAiming)
			{
				Next.m_TargetX = (int)Controls.m_aMousePos[Dummy].x;
				Next.m_TargetY = (int)Controls.m_aMousePos[Dummy].y;
			}
		}
		Changed[Dummy] = Next.m_Direction != m_aInput[Dummy].m_Direction || Next.m_Hook != m_aInput[Dummy].m_Hook ||
			Next.m_Fire != m_aInput[Dummy].m_Fire || Next.m_Jump != m_aInput[Dummy].m_Jump ||
			Next.m_NextWeapon != m_aInput[Dummy].m_NextWeapon || Next.m_PrevWeapon != m_aInput[Dummy].m_PrevWeapon ||
			Next.m_WantedWeapon != m_aInput[Dummy].m_WantedWeapon;
		m_aInput[Dummy] = Next;
	}
	return Changed[0] || Changed[1];
}

void CCloudInput::ApplyOffset(const CGameClient &GameClient, int ClientId, int &Tick, float &Intra) const
{
	if(!IsActive() || (!GameClient.IsFastInputLocalClient(ClientId) && (!g_Config.m_BcCloudInputOthers || !GameClient.m_ReceivedPreInput))) return;
	const float Offset = GameClient.IsFastInputLocalClient(ClientId) ? Amount() : (float)OthersTickOffset();
	const float Total = (Tick - 1) + Intra + Offset;
	Tick = (int)Total + 1;
	Intra = Total - (int)Total;
	if(Intra < 0.0f && Tick > 0)
	{
		Tick -= 1;
		Intra += 1.0f;
	}
}

bool CCloudInput::TryGetPredPos(const CGameClient &GameClient, int ClientId, int Tick, float Intra, vec2 &OutPos) const
{
	if(!IsActive() || Tick <= 0) return false;
	const int TickOffset = GameClient.IsFastInputLocalClient(ClientId) ? SelfTickOffset() : OthersTickOffset();
	if(Tick > GameClient.Client()->PredGameTick(g_Config.m_ClDummy) + TickOffset ||
		GameClient.m_aClients[ClientId].m_aPredTick[(Tick - 1) % 200] != Tick - 1 ||
		GameClient.m_aClients[ClientId].m_aPredTick[Tick % 200] != Tick) return false;
	OutPos = mix(GameClient.m_aClients[ClientId].m_aPredPos[(Tick - 1) % 200], GameClient.m_aClients[ClientId].m_aPredPos[Tick % 200], Intra);
	return true;
}
