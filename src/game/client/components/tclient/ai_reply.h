#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AI_REPLY_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AI_REPLY_H

#include "ai_reply_util.h"

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>

class IHttpRequest;

// All game state is accessed on the game thread. HTTP owns its asynchronous
// request; no callbacks capture this component or any player/message pointers.
class CAiReply : public CComponent
{
	struct SSettings
	{
		bool m_Enabled = false;
		bool m_Context = false;
		bool m_WebSearch = false;
		bool m_AllowInsecure = false;
		int m_Protocol = AiReply::OPENAI;
		int m_ContextTurns = 6;
		int m_Cooldown = 5;
		std::string m_Endpoint;
		std::string m_Key;
		std::string m_Model;
		std::string m_Prompt;

		bool operator==(const SSettings &Other) const;
	};

	struct SPeer
	{
		uint64_t m_Identity = 0;
		std::string m_Name;
		std::string m_Clan;
		int64_t m_NextAdmission = 0;
		// Never disclose team-chat history in a public-channel request.
		std::array<std::deque<AiReply::SExchange>, 2> m_aHistory;
		std::deque<int> m_HistoryOrder;
	};

	struct SMessage
	{
		int m_Attempts = 0;
		int m_ClientId = -1;
		int m_Channel = 0;
		int m_Connection = 0;
		int m_LocalId = -1;
		int m_LocalTeam = 0;
		int m_LocalDDTeam = 0;
		int m_SenderTeam = 0;
		int m_SenderDDTeam = 0;
		uint64_t m_Identity = 0;
		int64_t m_QueuedAt = 0;
		std::string m_Original;
		std::string m_Body;
	};

	static constexpr size_t MAX_QUEUE = 8;
	static constexpr int QUEUE_LIFETIME_SECONDS = 120;

	SSettings m_Settings;
	bool m_HaveSettings = false;
	const char *m_pConfigError = nullptr;
	const char *m_pStatus = "AI 自动回复已关闭";
	std::array<SPeer, MAX_CLIENTS> m_aPeers;
	uint64_t m_NextIdentity = 0;
	std::deque<SMessage> m_Queue;
	std::optional<SMessage> m_InFlight;
	std::shared_ptr<IHttpRequest> m_pRequest;
	std::shared_ptr<IHttpRequest> m_pSearchRequest;
	std::string m_Reply;
	int64_t m_NextRequest = 0;
	int64_t m_NextSend = 0;

	void SynchronizeSettings();
	void SynchronizePeers();
	void TrimPeerHistory(SPeer &Peer);
	bool Ready();
	bool PeerAllowed(int ClientId, const char *pMessage = nullptr) const;
	bool MessageCurrent(const SMessage &Message) const;
	void CancelActive();
	void StartRequest(SMessage Message, const std::string &WebContext = {}, bool SearchComplete = false);

public:
	~CAiReply() override;
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnUpdate() override;
	void OnReset() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnMessage(int MsgType, void *pRawMsg) override;

	// Settings UI may cancel immediately after an edit; console changes are
	// detected in OnUpdate/OnMessage before any request or reply is processed.
	void OnConfigChanged();
	void Cancel();
	void ClearContext();
	size_t ContextSize() const;
	const char *Status() const { return m_pStatus; }
	size_t Pending() const { return m_Queue.size() + (m_InFlight ? 1 : 0); }
};

#endif
