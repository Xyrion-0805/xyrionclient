#include "netease_lyric_timeline.h"

#include <algorithm>
#include <limits>

namespace NeteaseLyrics
{
	bool AreTimelinesEquivalent(const STimeline &Left, const STimeline &Right)
	{
		if(Left.m_HasTiming != Right.m_HasTiming || Left.m_vLines.size() != Right.m_vLines.size())
			return false;
		for(size_t Index = 0; Index < Left.m_vLines.size(); ++Index)
		{
			const SLine &LeftLine = Left.m_vLines[Index];
			const SLine &RightLine = Right.m_vLines[Index];
			if(LeftLine.m_StartMs != RightLine.m_StartMs || LeftLine.m_EndMs != RightLine.m_EndMs || LeftLine.m_Text != RightLine.m_Text || LeftLine.m_vWords.size() != RightLine.m_vWords.size())
				return false;
			for(size_t WordIndex = 0; WordIndex < LeftLine.m_vWords.size(); ++WordIndex)
			{
				const SWord &LeftWord = LeftLine.m_vWords[WordIndex];
				const SWord &RightWord = RightLine.m_vWords[WordIndex];
				if(LeftWord.m_StartMs != RightWord.m_StartMs || LeftWord.m_EndMs != RightWord.m_EndMs || LeftWord.m_Text != RightWord.m_Text)
					return false;
			}
		}
		return true;
	}

	SSelectedLine SelectCurrentLine(const STimeline &Timeline, int64_t PositionMs)
	{
		SSelectedLine Result;
		if(!Timeline.m_HasTiming || Timeline.m_vLines.empty() || PositionMs < 0)
			return Result;

		const auto It = std::upper_bound(Timeline.m_vLines.begin(), Timeline.m_vLines.end(), PositionMs, [](int64_t Position, const SLine &Line) {
			return Position < Line.m_StartMs;
		});
		if(It == Timeline.m_vLines.begin())
			return Result;
		const int Index = (int)std::distance(Timeline.m_vLines.begin(), It) - 1;
		const SLine &Line = Timeline.m_vLines[(size_t)Index];
		if(Line.m_EndMs >= 0 && PositionMs >= Line.m_EndMs)
			return Result;
		Result.m_Index = Index;
		Result.m_pLine = &Line;
		Result.m_InTimedRange = true;
		return Result;
	}

	SSelectedLine SelectLatestStartedLine(const STimeline &Timeline, int64_t PositionMs)
	{
		SSelectedLine Result;
		if(!Timeline.m_HasTiming || Timeline.m_vLines.empty() || PositionMs < 0)
			return Result;

		const auto It = std::upper_bound(Timeline.m_vLines.begin(), Timeline.m_vLines.end(), PositionMs, [](int64_t Position, const SLine &Line) {
			return Position < Line.m_StartMs;
		});
		if(It == Timeline.m_vLines.begin())
			return Result;
		const int Index = (int)std::distance(Timeline.m_vLines.begin(), It) - 1;
		const SLine &Line = Timeline.m_vLines[(size_t)Index];
		Result.m_Index = Index;
		Result.m_pLine = &Line;
		Result.m_InTimedRange = Line.m_EndMs < 0 || PositionMs < Line.m_EndMs;
		return Result;
	}

	void SPlaybackAnchor::Reset()
	{
		m_PositionMs = 0;
		m_ReceivedAt = {};
		m_Playing = false;
		m_Valid = false;
	}

	void SPlaybackAnchor::Update(int64_t PositionMs, bool Playing, std::chrono::steady_clock::time_point ReceivedAt)
	{
		m_PositionMs = std::max<int64_t>(0, PositionMs);
		m_ReceivedAt = ReceivedAt;
		m_Playing = Playing;
		m_Valid = true;
	}

	int64_t SPlaybackAnchor::Estimate(std::chrono::steady_clock::time_point Now) const
	{
		if(!m_Valid)
			return 0;
		if(!m_Playing || Now <= m_ReceivedAt)
			return m_PositionMs;
		const auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Now - m_ReceivedAt).count();
		if(Elapsed <= 0 || m_PositionMs > std::numeric_limits<int64_t>::max() - Elapsed)
			return m_PositionMs;
		return m_PositionMs + Elapsed;
	}

	bool SGenerationState::UpdateSong(uint64_t SongId)
	{
		if(m_HasSong && m_SongId == SongId)
			return false;
		if(m_Generation == std::numeric_limits<uint64_t>::max())
			m_Generation = 1;
		else
			++m_Generation;
		m_SongId = SongId;
		m_HasSong = SongId != 0;
		return true;
	}

	void SGenerationState::Clear()
	{
		m_SongId = 0;
		m_HasSong = false;
	}

	bool IsSnapshotStale(uint64_t UpdatedAtTick, uint64_t NowTick, uint64_t TimeoutMs)
	{
		if(UpdatedAtTick == 0 || NowTick < UpdatedAtTick)
			return true;
		return NowTick - UpdatedAtTick > TimeoutMs;
	}
} // namespace NeteaseLyrics
