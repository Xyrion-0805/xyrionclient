#include <game/client/components/controls.h>

#include <engine/shared/config.h>
#include <game/client/gameclient.h>

void CControls::ResetPixelMove()
{
	m_PixelRequests.clear();
	m_PixelClientId = m_PixelConnection = -1;
	m_PixelPulseTick = -1;
	m_PixelPulseDirection = 0;
}

void CControls::ConPixelMove(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CControls *>(pUserData);
	const bool Pressed = pResult->GetInteger(0) != 0;
	if(Pressed && !pSelf->m_PixelToggleHeld)
	{
		g_Config.m_XcPixelMove ^= 1;
		pSelf->ResetPixelMove();
		pSelf->GameClient()->m_Chat.Echo(g_Config.m_XcPixelMove ? "像素移动已开启" : "像素移动已关闭");
	}
	pSelf->m_PixelToggleHeld = Pressed;
}

void CControls::RequestPixelMove(int Direction)
{
	if(!g_Config.m_XcPixelMove || Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Menus.IsActive() || GameClient()->m_Chat.IsActive() ||
		GameClient()->m_GameConsole.IsActive() || GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->IsWorldPaused())
		return;
	const int Id = GameClient()->m_Snap.m_LocalClientId;
	if(Id < 0 || Id >= MAX_CLIENTS || !GameClient()->m_Snap.m_pLocalCharacter || (Direction != -1 && Direction != 1)) return;
	const int Connection = g_Config.m_ClDummy ? Client()->DummySelected() : 0;
	if(m_PixelClientId != Id || m_PixelConnection != Connection)
	{
		ResetPixelMove();
		m_PixelClientId = Id;
		m_PixelConnection = Connection;
	}
	if(m_PixelRequests.size() < 16)
	{
		m_PixelRequests.push_back(Direction);
		m_pPixelStatus = "等待移动";
	}
}

bool CControls::OnInput(const IInput::CEvent &Event)
{
	if(Event.m_Key != KEY_LEFT && Event.m_Key != KEY_RIGHT) return false;
	const int Index = Event.m_Key == KEY_LEFT ? 0 : 1;
	const bool WasHeld = m_aPixelArrowHeld[Index];
	if(Event.m_Flags & IInput::FLAG_RELEASE)
	{
		m_aPixelArrowHeld[Index] = false;
		if(WasHeld) return true;
	}
	if(!g_Config.m_XcPixelMove || Client()->State() != IClient::STATE_ONLINE ||
		GameClient()->m_Menus.IsActive() || GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive()) return false;
	const char *pCommand = GameClient()->m_Binds.Get(Event.m_Key, CBinds::GetModifierMask(Input()));
	if(pCommand[0] || GameClient()->m_Binds.Get(Event.m_Key, KeyModifier::NONE)[0]) return false;
	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(!WasHeld) RequestPixelMove(Index == 0 ? -1 : 1);
		m_aPixelArrowHeld[Index] = true;
	}
	return true;
}

int CControls::PixelMoveDirection()
{
	const int Id = GameClient()->m_Snap.m_LocalClientId;
	const int Connection = g_Config.m_ClDummy ? Client()->DummySelected() : 0;
	if(!g_Config.m_XcPixelMove || Client()->State() != IClient::STATE_ONLINE || Id < 0 || Id >= MAX_CLIENTS ||
		!GameClient()->m_Snap.m_pLocalCharacter || GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->IsWorldPaused() ||
		GameClient()->m_Menus.IsActive() || GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive() ||
		m_PixelClientId != Id || m_PixelConnection != Connection)
	{
		ResetPixelMove();
		return 0;
	}

	const int Tick = Client()->PredGameTick(g_Config.m_ClDummy);
	// Repeated snapshots for the same tick must not consume another key press.
	if(Tick == m_PixelPulseTick) return m_PixelPulseDirection;
	if(Tick < m_PixelPulseTick)
	{
		ResetPixelMove();
		return 0;
	}

	// A tap is one tick of the requested direction followed by a neutral tick.
	// Position and velocity are left entirely to normal game physics.
	const bool Release = m_PixelPulseDirection != 0;
	m_PixelPulseTick = Tick;
	m_PixelPulseDirection = 0;
	if(!Release && !m_PixelRequests.empty())
	{
		m_PixelPulseDirection = m_PixelRequests.front();
		m_PixelRequests.pop_front();
		m_pPixelStatus = "正在同方向短按";
	}
	else if(Release)
		m_pPixelStatus = "已松开，按住不连发";
	return m_PixelPulseDirection;
}
