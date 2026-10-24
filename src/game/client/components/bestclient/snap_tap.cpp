/* Copyright © 2026 BestProject Team */
#include "snap_tap.h"

#include <base/str.h>
#include <base/time.h>
#include <engine/shared/config.h>
#include <engine/serverbrowser.h>
#include <game/client/gameclient.h>

void CSnapTap::OnReset() { Reset(0); Reset(1); }
void CSnapTap::Reset(int Dummy)
{
	m_aApplied[Dummy] = m_aLastPressed[Dummy] = m_aPrevLeft[Dummy] = m_aPrevRight[Dummy] = 0;
	m_aLastPressedTime[Dummy] = 0;
}
void CSnapTap::Update(int Dummy, bool Left, bool Right)
{
	if(Left && !m_aPrevLeft[Dummy]) { m_aLastPressed[Dummy] = -1; m_aLastPressedTime[Dummy] = time_get(); }
	if(Right && !m_aPrevRight[Dummy]) { m_aLastPressed[Dummy] = 1; m_aLastPressedTime[Dummy] = time_get(); }
	m_aPrevLeft[Dummy] = Left;
	m_aPrevRight[Dummy] = Right;
}
int CSnapTap::ResolveMovementDirection(int Dummy, bool Left, bool Right, bool UpdateState)
{
	if(UpdateState) Update(Dummy, Left, Right);
	return Resolve(Dummy, Left, Right);
}
int CSnapTap::Resolve(int Dummy, bool Left, bool Right)
{
	if(Left != Right) return m_aApplied[Dummy] = Left ? -1 : 1;
	if(!Left) return m_aApplied[Dummy] = 0;
	if(!g_Config.m_BcSnapTap || IsBlockedByCommunity()) return 0;
	int Candidate = m_aLastPressed[Dummy];
	if(Candidate != -1 && Candidate != 1) Candidate = m_aApplied[Dummy] ? m_aApplied[Dummy] : -1;
	const int64_t Delay = time_freq() * (int64_t)g_Config.m_BcSnapTapDelay / 1000;
	if(!m_aApplied[Dummy] || (m_aApplied[Dummy] != Candidate && time_get() - m_aLastPressedTime[Dummy] >= Delay))
		m_aApplied[Dummy] = Candidate;
	return m_aApplied[Dummy];
}
bool CSnapTap::IsBlockedByCommunity() const
{
	auto IsBlockedGameType = [](const char *pGameType) {
		return pGameType && pGameType[0] &&
			(str_find_nocase(pGameType, "ddracenet") != nullptr || str_find_nocase(pGameType, "0xf") != nullptr);
	};

	const CServerInfo &ServerInfo = Client()->ServerInfo();
	const char *pCommunityId = ServerInfo.m_aCommunityId[0] ? ServerInfo.m_aCommunityId : nullptr;
	if(!pCommunityId && GameClient()->m_ConnectServerInfo.has_value() && GameClient()->m_ConnectServerInfo->m_aCommunityId[0])
		pCommunityId = GameClient()->m_ConnectServerInfo->m_aCommunityId;

	const auto *pEntry = ServerBrowser()->Find(Client()->ServerAddress());
	if(!pCommunityId && pEntry && pEntry->m_Info.m_aCommunityId[0])
		pCommunityId = pEntry->m_Info.m_aCommunityId;

	if(pCommunityId && str_comp_nocase(pCommunityId, IServerBrowser::COMMUNITY_DDNET) == 0)
		return true;
	if(IsBlockedGameType(ServerInfo.m_aGameType))
		return true;
	if(GameClient()->m_ConnectServerInfo.has_value() && IsBlockedGameType(GameClient()->m_ConnectServerInfo->m_aGameType))
		return true;
	if(pEntry && IsBlockedGameType(pEntry->m_Info.m_aGameType))
		return true;
	return IsBlockedGameType(GameClient()->m_GameInfo.m_aGameType);
}
