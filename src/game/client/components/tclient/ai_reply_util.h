#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_AI_REPLY_UTIL_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_AI_REPLY_UTIL_H

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace AiReply
{
	enum EProtocol
	{
		OPENAI = 0,
		ANTHROPIC = 1,
	};

	// These limits also bound allocations and work performed on the game thread.
	constexpr size_t MAX_INPUT_BYTES = 1024;
	constexpr size_t MAX_RESPONSE_BYTES = 64 * 1024;
	constexpr size_t MAX_CHAT_BYTES = 255;
	constexpr int MAX_CONTEXT_TURNS = 32;
	// Matches the 1024-byte endpoint configuration and parent HTTP URL buffer.
	constexpr size_t MAX_ENDPOINT_BYTES = 1023;
	inline constexpr const char *DEFAULT_SYSTEM_PROMPT = "你是游戏聊天助手。用简短自然的中文回应玩家，最多一句话，不输出命令、代码或长篇解释。";

	struct SExchange
	{
		std::string m_User;
		std::string m_Assistant;
	};

	struct SRequestSettings
	{
		int m_Protocol = OPENAI;
		std::string m_Model;
		std::string m_Prompt;
		std::string m_WebContext;
		int m_ContextTurns = 6;
	};

	// Exact, case-sensitive UTF-8 names. Prefixes such as [Name]:, Name: and
	// @Name: (including full-width punctuation) are removed. Interior mentions
	// need explicit boundaries; substrings inside another name do not count.
	// When provided, an exact known [Sender]: wrapper is stripped first.
	std::optional<std::string> ExtractAddressedBody(const std::string &Message, const std::vector<std::string> &vNames, const std::string &SenderName = {});

	// No callbacks, network, game state or credentials in the request body.
	std::optional<std::string> BuildRequest(const SRequestSettings &Settings, const std::deque<SExchange> &History, const std::string &User);
	std::optional<std::vector<std::string>> BuildHeaders(int Protocol, const std::string &Key);
	bool IsValidEndpoint(const std::string &Endpoint);

	// Decode only assistant text. Never turn provider errors/tool calls into chat.
	std::optional<std::string> ParseResponse(int Protocol, const std::string &Json);
	std::string ParseSearchResults(const std::string &Xml);
	// Flatten whitespace, remove controls, prefix slash commands and truncate
	// at a complete UTF-8 codepoint. MaxBytes excludes the trailing NUL.
	std::string SanitizeReply(const std::string &Text, size_t MaxBytes = MAX_CHAT_BYTES);
	void TrimHistory(std::deque<SExchange> &History, int MaxTurns);
}

#endif
