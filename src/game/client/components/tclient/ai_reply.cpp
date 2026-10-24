#include "ai_reply.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/http.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

bool CAiReply::SSettings::operator==(const SSettings &Other) const
{
	return std::tie(m_Enabled, m_Context, m_WebSearch, m_AllowInsecure, m_Protocol, m_ContextTurns, m_Cooldown, m_Endpoint, m_Key, m_Model, m_Prompt) ==
		std::tie(Other.m_Enabled, Other.m_Context, Other.m_WebSearch, Other.m_AllowInsecure, Other.m_Protocol, Other.m_ContextTurns, Other.m_Cooldown, Other.m_Endpoint, Other.m_Key, Other.m_Model, Other.m_Prompt);
}

CAiReply::~CAiReply()
{
	CancelActive();
}

void CAiReply::CancelActive()
{
	if(m_pSearchRequest)
		m_pSearchRequest->Abort();
	m_pSearchRequest.reset();
	if(m_pRequest)
		m_pRequest->Abort();
	// IHttp keeps ownership until its worker finishes aborting. Never Wait().
	m_pRequest.reset();
	m_InFlight.reset();
	m_Reply.clear();
}

void CAiReply::Cancel()
{
	CancelActive();
	m_Queue.clear();
	for(auto &Peer : m_aPeers)
		Peer = SPeer{};
	m_NextRequest = 0;
	m_NextSend = 0;
	m_pStatus = g_Config.m_XcAiEnabled ? "等待提及自己的消息" : "AI 自动回复已关闭";
}

void CAiReply::ClearContext()
{
	// A pending request already contains its context. Drop it as well, so
	// clearing cannot be undone by a delayed response repopulating history.
	CancelActive();
	m_Queue.clear();
	for(auto &Peer : m_aPeers)
	{
		for(auto &History : Peer.m_aHistory)
			History.clear();
		Peer.m_HistoryOrder.clear();
	}
	m_pStatus = "上下文已清空";
}

size_t CAiReply::ContextSize() const
{
	size_t Size = 0;
	for(const auto &Peer : m_aPeers)
		for(const auto &History : Peer.m_aHistory)
			Size += History.size();
	return Size;
}

void CAiReply::OnConfigChanged()
{
	Cancel();
	m_HaveSettings = false;
}

void CAiReply::OnInit()
{
	SynchronizeSettings();
}

void CAiReply::OnReset()
{
	Cancel();
}

void CAiReply::OnShutdown()
{
	Cancel();
}

void CAiReply::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE || OldState != IClient::STATE_ONLINE)
		Cancel();
}

void CAiReply::SynchronizeSettings()
{
	SSettings Next;
	Next.m_Enabled = g_Config.m_XcAiEnabled != 0;
	Next.m_Context = g_Config.m_XcAiContext != 0;
	Next.m_WebSearch = g_Config.m_XcAiWebSearch != 0;
	Next.m_AllowInsecure = g_Config.m_HttpAllowInsecure != 0;
	Next.m_Protocol = g_Config.m_XcAiProtocol;
	Next.m_ContextTurns = std::clamp(g_Config.m_XcAiContextTurns, 1, AiReply::MAX_CONTEXT_TURNS);
	Next.m_Cooldown = std::clamp(g_Config.m_XcAiCooldown, 0, 120);
	Next.m_Endpoint = g_Config.m_XcAiEndpoint;
	Next.m_Key = g_Config.m_XcAiKey;
	Next.m_Model = g_Config.m_XcAiModel;
	Next.m_Prompt = g_Config.m_XcAiPrompt;
	if(m_HaveSettings && Next == m_Settings)
		return;

	Cancel();
	m_Settings = std::move(Next);
	m_HaveSettings = true;
	m_pConfigError = nullptr;
	if(!AiReply::IsValidEndpoint(m_Settings.m_Endpoint))
		m_pConfigError = "请输入完整的 HTTP(S) 接口地址（最多 1023 字节）";
	else if(!AiReply::BuildHeaders(m_Settings.m_Protocol, m_Settings.m_Key))
		m_pConfigError = "协议或 API 密钥无效（密钥不能含空格或控制字符）";
	else
	{
		AiReply::SRequestSettings RequestSettings;
		RequestSettings.m_Protocol = m_Settings.m_Protocol;
		RequestSettings.m_Model = m_Settings.m_Model;
		RequestSettings.m_Prompt = m_Settings.m_Prompt;
		RequestSettings.m_ContextTurns = 0;
		if(!AiReply::BuildRequest(RequestSettings, {}, "check"))
			m_pConfigError = "请检查模型名称和系统提示词";
	}
}

bool CAiReply::Ready()
{
	if(!m_Settings.m_Enabled)
	{
		m_pStatus = "AI 自动回复已关闭";
		return false;
	}
	if(Client()->State() != IClient::STATE_ONLINE || GameClient()->m_SuppressEvents)
	{
		m_pStatus = "仅在联机游戏中启用";
		return false;
	}
	if(!m_Settings.m_AllowInsecure && m_Settings.m_Endpoint.compare(0, 7, "http://") == 0)
	{
		m_pStatus = "HTTP 地址需要启用 http_allow_insecure 1";
		return false;
	}
	if(m_pConfigError)
	{
		m_pStatus = m_pConfigError;
		return false;
	}
	return true;
}

bool CAiReply::PeerAllowed(int ClientId, const char *pMessage) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	const auto &Peer = GameClient()->m_aClients[ClientId];
	if(!Peer.m_Active || Peer.m_aName[0] == '\0' || Peer.m_ChatIgnore || Peer.m_Foe ||
		ClientId == GameClient()->m_Snap.m_LocalClientId)
		return false;
	for(size_t i = 0; i < std::size(GameClient()->m_aLocalIds); ++i)
		if((i == 0 || Client()->DummyConnected()) && ClientId == GameClient()->m_aLocalIds[i])
			return false;
	if(g_Config.m_ClShowChatFriends && !Peer.m_Friend)
		return false;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(g_Config.m_ClShowChatTeamMembersOnly && LocalId >= 0 && LocalId < MAX_CLIENTS &&
		GameClient()->IsOtherTeam(ClientId) && GameClient()->m_Teams.Team(LocalId) != TEAM_FLOCK)
		return false;
	if(pMessage)
	{
		auto &Regex = GameClient()->m_TClient.m_RegexChatIgnore;
		if(Regex.error().empty() && Regex.test(pMessage))
			return false;
	}
	return true;
}

void CAiReply::SynchronizePeers()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		auto &Peer = m_aPeers[ClientId];
		if(!PeerAllowed(ClientId))
		{
			Peer = SPeer{};
			continue;
		}
		const auto &ClientData = GameClient()->m_aClients[ClientId];
		if(Peer.m_Identity == 0 || Peer.m_Name != ClientData.m_aName || Peer.m_Clan != ClientData.m_aClan)
		{
			Peer = SPeer{};
			Peer.m_Identity = ++m_NextIdentity;
			Peer.m_Name = ClientData.m_aName;
			Peer.m_Clan = ClientData.m_aClan;
		}
		TrimPeerHistory(Peer);
	}
}

void CAiReply::TrimPeerHistory(SPeer &Peer)
{
	const size_t Limit = m_Settings.m_Context ? m_Settings.m_ContextTurns : 0;
	// The turn limit applies to the peer as a whole, across both channels.
	// Individual channels are kept separate when composing a request.
	while(Peer.m_HistoryOrder.size() > Limit)
	{
		Peer.m_aHistory[Peer.m_HistoryOrder.front()].pop_front();
		Peer.m_HistoryOrder.pop_front();
	}
}

bool CAiReply::MessageCurrent(const SMessage &Message) const
{
	if(!PeerAllowed(Message.m_ClientId, Message.m_Original.c_str()) ||
		m_aPeers[Message.m_ClientId].m_Identity != Message.m_Identity ||
		Message.m_Connection != g_Config.m_ClDummy ||
		Message.m_LocalId != GameClient()->m_Snap.m_LocalClientId)
		return false;
	if(Message.m_LocalId < 0 || Message.m_LocalId >= MAX_CLIENTS)
		return false;
	if(Message.m_Channel == 1)
	{
		// A delayed team reply must not land in a different team after a swap.
		if(GameClient()->m_aClients[Message.m_LocalId].m_Team != Message.m_LocalTeam ||
			GameClient()->m_Teams.Team(Message.m_LocalId) != Message.m_LocalDDTeam ||
			GameClient()->m_aClients[Message.m_ClientId].m_Team != Message.m_SenderTeam ||
			GameClient()->m_Teams.Team(Message.m_ClientId) != Message.m_SenderDDTeam)
			return false;
	}
	return true;
}

void CAiReply::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType != NETMSGTYPE_SV_CHAT || pRawMsg == nullptr)
		return;
	SynchronizeSettings();
	if(!Ready())
		return;
	const auto *pMessage = static_cast<const CNetMsg_Sv_Chat *>(pRawMsg);
	// 2/3 are sent/received whispers, not valid SendChat channels. Ignore them
	// rather than leaking a private response into public chat on 0.7.
	if((pMessage->m_Team != 0 && pMessage->m_Team != 1) || pMessage->m_pMessage == nullptr ||
		!PeerAllowed(pMessage->m_ClientId, pMessage->m_pMessage))
		return;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return;

	std::vector<std::string> vNames = {Client()->PlayerName()};
	if(Client()->DummyConnected())
		vNames.emplace_back(Client()->DummyName());
	for(size_t i = 0; i < std::size(GameClient()->m_aLocalIds); ++i)
	{
		if(i != 0 && !Client()->DummyConnected())
			continue;
		const int Id = GameClient()->m_aLocalIds[i];
		if(Id >= 0 && Id < MAX_CLIENTS && GameClient()->m_aClients[Id].m_Active)
			vNames.emplace_back(GameClient()->m_aClients[Id].m_aName);
	}
	vNames.emplace_back(GameClient()->m_aClients[LocalId].m_aName);
	const auto Body = AiReply::ExtractAddressedBody(pMessage->m_pMessage, vNames, GameClient()->m_aClients[pMessage->m_ClientId].m_aName);
	if(!Body)
		return;

	SynchronizePeers();
	auto &Peer = m_aPeers[pMessage->m_ClientId];
	const int64_t Now = time();
	if(Now < Peer.m_NextAdmission)
		return;

	SMessage Message;
	Message.m_ClientId = pMessage->m_ClientId;
	Message.m_Channel = pMessage->m_Team;
	Message.m_Connection = g_Config.m_ClDummy;
	Message.m_LocalId = LocalId;
	Message.m_LocalTeam = GameClient()->m_aClients[LocalId].m_Team;
	Message.m_LocalDDTeam = GameClient()->m_Teams.Team(LocalId);
	Message.m_SenderTeam = GameClient()->m_aClients[pMessage->m_ClientId].m_Team;
	Message.m_SenderDDTeam = GameClient()->m_Teams.Team(pMessage->m_ClientId);
	Message.m_Identity = Peer.m_Identity;
	Message.m_QueuedAt = Now;
	Message.m_Original = pMessage->m_pMessage;
	Message.m_Body = *Body;

	// At most one queued request per peer/channel; update it with the latest
	// eligible message instead of growing the queue during a burst.
	auto Queued = std::find_if(m_Queue.begin(), m_Queue.end(), [&](const SMessage &Other) {
		return Other.m_ClientId == Message.m_ClientId && Other.m_Channel == Message.m_Channel;
	});
	if(Queued != m_Queue.end())
		*Queued = std::move(Message);
	else if(m_Queue.size() < MAX_QUEUE)
		m_Queue.push_back(std::move(Message));
	else
		return;
	Peer.m_NextAdmission = Now + static_cast<int64_t>(m_Settings.m_Cooldown) * time_freq();
	if(!m_InFlight)
		m_pStatus = "请求已加入队列";
}

void CAiReply::StartRequest(SMessage Message, const std::string &WebContext, bool SearchComplete)
{
	if(m_Settings.m_WebSearch && !SearchComplete && Http())
	{
		char aQuery[700];
		EscapeUrl(aQuery, AiReply::SanitizeReply(Message.m_Body, 200).c_str());
		const std::string Url = std::string("https://www.bing.com/search?format=rss&q=") + aQuery;
		m_pSearchRequest = HttpGet(Url.c_str());
		m_pSearchRequest->LogProgress(HTTPLOG::NONE);
		m_pSearchRequest->Timeout(CTimeout{4000, 10000, 100, 5});
		m_pSearchRequest->MaxResponseSize(AiReply::MAX_RESPONSE_BYTES);
		m_pSearchRequest->FailOnErrorStatus(true);
		m_InFlight = std::move(Message);
		m_pStatus = "正在联网搜索";
		Http()->Run(m_pSearchRequest);
		return;
	}
	AiReply::SRequestSettings RequestSettings;
	RequestSettings.m_Protocol = m_Settings.m_Protocol;
	RequestSettings.m_Model = m_Settings.m_Model;
	RequestSettings.m_Prompt = m_Settings.m_Prompt;
	RequestSettings.m_ContextTurns = m_Settings.m_Context ? m_Settings.m_ContextTurns : 0;
	RequestSettings.m_WebContext = WebContext;
	const auto Body = AiReply::BuildRequest(RequestSettings, m_aPeers[Message.m_ClientId].m_aHistory[Message.m_Channel], Message.m_Body);
	const auto Headers = AiReply::BuildHeaders(m_Settings.m_Protocol, m_Settings.m_Key);
	if(!Body || !Headers || Http() == nullptr)
	{
		m_pStatus = "无法创建 AI 请求";
		return;
	}

	auto pRequest = CreateHttpRequest(m_Settings.m_Endpoint.c_str());
	pRequest->LogProgress(HTTPLOG::NONE);
	pRequest->Timeout(CTimeout{15000, 90000, 0, 0});
	pRequest->MaxResponseSize(AiReply::MAX_RESPONSE_BYTES);
	pRequest->FailOnErrorStatus(false);
	// HeaderString uses a 256-byte scratch buffer. Header accepts the full
	// string and therefore preserves keys up to the 512-byte config limit.
	for(const auto &Header : *Headers)
		pRequest->Header(Header.c_str());
	// Body owns the CJsonStringWriter output until PostJson has copied it.
	// Finalize every setting/body/header before handing ownership to IHttp.
	pRequest->PostJson(Body->c_str());
	m_InFlight = std::move(Message);
	m_pRequest = std::move(pRequest);
	m_pStatus = !m_Settings.m_WebSearch ? "正在请求 AI 回复" : (WebContext.empty() ? "联网搜索无结果，正在请求 AI 回复" : "已获取网页资料，正在请求 AI 回复");
	m_NextRequest = time() + static_cast<int64_t>(std::max(1, m_Settings.m_Cooldown)) * time_freq();
	Http()->Run(m_pRequest);
}

void CAiReply::OnUpdate()
{
	SynchronizeSettings();
	if(!Ready())
	{
		// Keep Ready's actionable status after clearing all transient state.
		const char *pStatus = m_pStatus;
		if(m_InFlight || !m_Queue.empty())
			Cancel();
		m_pStatus = pStatus;
		return;
	}
	SynchronizePeers();
	const int64_t Now = time();
	m_Queue.erase(std::remove_if(m_Queue.begin(), m_Queue.end(), [&](const SMessage &Message) {
		return !MessageCurrent(Message) || Now - Message.m_QueuedAt > static_cast<int64_t>(QUEUE_LIFETIME_SECONDS) * time_freq();
	}), m_Queue.end());

	if(m_InFlight && !MessageCurrent(*m_InFlight))
		CancelActive();
	if(m_pSearchRequest)
	{
		if(!m_pSearchRequest->Done())
			return;
		std::string WebContext;
		if(m_pSearchRequest->State() == EHttpState::DONE && m_pSearchRequest->StatusCode() == 200)
		{
			unsigned char *pResult = nullptr;
			size_t Length = 0;
			m_pSearchRequest->Result(&pResult, &Length);
			if(pResult && Length <= AiReply::MAX_RESPONSE_BYTES)
				WebContext = AiReply::ParseSearchResults(std::string(reinterpret_cast<const char *>(pResult), Length));
		}
		m_pSearchRequest.reset();
		SMessage Message = std::move(*m_InFlight);
		m_InFlight.reset();
		StartRequest(std::move(Message), WebContext, true);
	}
	if(m_pRequest && m_pRequest->Done())
	{
		// Done() also includes transport failures and aborted requests. Only a
		// completed HTTP response permits reading StatusCode() or Result().
		const int Code = m_pRequest->State() == EHttpState::DONE ? m_pRequest->StatusCode() : 0;
		if(Code >= 200 && Code < 300)
		{
			unsigned char *pResult = nullptr;
			size_t Length = 0;
			m_pRequest->Result(&pResult, &Length);
			if(pResult && Length <= AiReply::MAX_RESPONSE_BYTES)
			{
				const auto Text = AiReply::ParseResponse(m_Settings.m_Protocol, std::string(reinterpret_cast<const char *>(pResult), Length));
				if(Text)
					m_Reply = AiReply::SanitizeReply(*Text, MAX_CHAT_LENGTH - 1);
			}
			m_pStatus = "服务返回的回复为空或格式无效";
		}
		else
		{
			const bool Temporary = Code == 0 || Code == 408 || Code == 429 || Code >= 500;
			if(Temporary && m_InFlight && m_InFlight->m_Attempts == 0 && MessageCurrent(*m_InFlight))
			{
				++m_InFlight->m_Attempts;
				m_Queue.push_front(std::move(*m_InFlight));
				m_NextRequest = Now + (Code == 429 ? 15 : 3) * time_freq();
				m_pStatus = "服务暂不可用，等待自动重试一次";
			}
			else if(Code == 401 || Code == 403) m_pStatus = "AI 认证失败：检查 API 密钥及模型权限";
			else if(Code == 404) m_pStatus = "AI 接口不存在：检查完整请求路径及模型名";
			else if(Code == 400 || Code == 422) m_pStatus = "AI 请求参数不兼容：检查协议与模型";
			else if(Code == 429) m_pStatus = "AI 服务限流或额度不足，请稍后重试";
			else if(Code == 0) m_pStatus = "AI 连接失败或超时：检查网络、代理和服务地址";
			else m_pStatus = "AI 服务暂不可用，自动重试未成功";
		}
		m_pRequest.reset();
		if(m_Reply.empty())
			m_InFlight.reset();
	}
	if(m_InFlight)
	{
		if(m_pRequest)
			return;
		if(Now < m_NextSend)
		{
			m_pStatus = "等待冷却时间";
			return;
		}
		// Recheck identity, mute state, active connection and team before send.
		if(MessageCurrent(*m_InFlight) && !m_Reply.empty())
		{
			GameClient()->m_Chat.SendChat(m_InFlight->m_Channel, m_Reply.c_str());
			if(m_Settings.m_Context)
			{
				auto &Peer = m_aPeers[m_InFlight->m_ClientId];
				Peer.m_aHistory[m_InFlight->m_Channel].push_back({m_InFlight->m_Body, m_Reply});
				Peer.m_HistoryOrder.push_back(m_InFlight->m_Channel);
				TrimPeerHistory(Peer);
			}
			m_NextSend = Now + static_cast<int64_t>(std::max(1, m_Settings.m_Cooldown)) * time_freq();
			m_pStatus = "回复已发送";
		}
		m_InFlight.reset();
		m_Reply.clear();
		return;
	}

	if(m_Queue.empty())
		return;
	if(Now < m_NextRequest || Now < m_NextSend)
	{
		m_pStatus = "等待冷却时间";
		return;
	}
	SMessage Message = std::move(m_Queue.front());
	m_Queue.pop_front();
	StartRequest(std::move(Message));
}
