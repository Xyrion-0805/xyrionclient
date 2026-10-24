#include "QmRt.h"
#include "../gameclient.h"
#include <base/str.h>
#include <cmath>

void CUiRuntimeV2::Init(CGameClient *pGameClient)
{
	m_pGameClient = pGameClient;
	Reset();
}

void CUiRuntimeV2::Reset()
{
	m_Tree.Reset();
	m_AnimRuntime.Reset();
	m_RenderBridge.BeginFrame();
	m_LastStats = {};
	m_DebugLogAccumulator = 0.0f;
	ClearPerfContext();
}

bool CUiRuntimeV2::Enabled() const
{
	return m_pGameClient != nullptr;
}

void CUiRuntimeV2::SetPerfContext(const char *pPage, const char *pOperation)
{
	str_copy(m_aPerfPage, pPage != nullptr ? pPage : "", sizeof(m_aPerfPage));
	str_copy(m_aPerfOperation, pOperation != nullptr ? pOperation : "", sizeof(m_aPerfOperation));
}

void CUiRuntimeV2::ClearPerfContext()
{
	m_aPerfPage[0] = '\0';
	m_aPerfOperation[0] = '\0';
}

const SUiV2PerfStats &CUiRuntimeV2::LastStats() const
{
	return m_LastStats;
}

void CUiRuntimeV2::OnRender()
{
	if(!Enabled())
		return;
	float Dt = m_pGameClient->Client()->RenderFrameTime();
	if(!std::isfinite(Dt) || Dt < 0.0f)
		Dt = 0.0f;
	m_FrameDt = Dt;
	m_Tree.BeginFrame();
	m_AnimRuntime.Advance(Dt);
	m_RenderBridge.BeginFrame();
	m_Tree.EndFrame(m_AnimRuntime);
	m_LastStats.m_NodeCount = m_Tree.NodeCount();
	m_LastStats.m_ActiveAnimCount = m_AnimRuntime.ActiveTrackCount();
	m_LastStats.m_QueuedAnimCount = m_AnimRuntime.QueuedTrackCount();
}
