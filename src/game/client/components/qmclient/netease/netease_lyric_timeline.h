#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_NETEASE_LYRIC_TIMELINE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_NETEASE_NETEASE_LYRIC_TIMELINE_H

#include "netease_lyric_parser.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace NeteaseLyrics
{
	struct SSelectedLine
	{
		int m_Index = -1;
		const SLine *m_pLine = nullptr;
		bool m_InTimedRange = false;
	};

	// 选择当前句。时间在第一句之前或明确的时间空洞中返回无效，不显示旧句。
	SSelectedLine SelectCurrentLine(const STimeline &Timeline, int64_t PositionMs);
	// 选择已经开始的最近一句,忽略该句结束时间,用于间奏中继续显示上一句的歌词。
	SSelectedLine SelectLatestStartedLine(const STimeline &Timeline, int64_t PositionMs);
	bool AreTimelinesEquivalent(const STimeline &Left, const STimeline &Right);

	struct SPlaybackAnchor
	{
		int64_t m_PositionMs = 0;
		std::chrono::steady_clock::time_point m_ReceivedAt{};
		bool m_Playing = false;
		bool m_Valid = false;

		void Reset();
		void Update(int64_t PositionMs, bool Playing, std::chrono::steady_clock::time_point ReceivedAt = std::chrono::steady_clock::now());
		int64_t Estimate(std::chrono::steady_clock::time_point Now = std::chrono::steady_clock::now()) const;
	};

	struct SGenerationState
	{
		uint64_t m_SongId = 0;
		uint64_t m_Generation = 0;
		bool m_HasSong = false;

		// 返回 true 表示歌曲发生变化；调用方应立即丢弃旧时间轴/当前句。
		bool UpdateSong(uint64_t SongId);
		void Clear();
	};

	bool IsSnapshotStale(uint64_t UpdatedAtTick, uint64_t NowTick, uint64_t TimeoutMs);

} // namespace NeteaseLyrics

namespace QmNetease
{
	using NeteaseLyrics::AreTimelinesEquivalent;
	using NeteaseLyrics::IsSnapshotStale;
	using NeteaseLyrics::SelectCurrentLine;
	using NeteaseLyrics::SelectLatestStartedLine;
	using NeteaseLyrics::SGenerationState;
	using NeteaseLyrics::SPlaybackAnchor;
	using NeteaseLyrics::SSelectedLine;
}

#endif
