#include "video_background.h"

#include <base/io.h>
#include <base/log.h>
#include <base/mem.h>

#include <engine/graphics.h>
#include <engine/storage.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

#if defined(CONF_VIDEORECORDER)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}

#include <cerrno>

namespace
{
	std::string AvError(const char *pOperation, int Error)
	{
		char aError[AV_ERROR_MAX_STRING_SIZE];
		av_strerror(Error, aError, sizeof(aError));
		return std::string(pOperation) + ": " + aError;
	}

	class CDecoder
	{
		std::atomic<bool> &m_Stop;
		IOHANDLE m_File = nullptr;
		int64_t m_FileSize = 0;
		AVIOContext *m_pIo = nullptr;
		bool m_PacketPending = false;
		bool m_InputEnded = false;
		bool m_DrainSent = false;

		static int Read(void *pUser, uint8_t *pBuffer, int Size)
		{
			auto *pSelf = static_cast<CDecoder *>(pUser);
			if(pSelf->m_Stop)
				return AVERROR_EXIT;
			const unsigned ReadSize = io_read(pSelf->m_File, pBuffer, Size);
			return ReadSize ? static_cast<int>(ReadSize) : (io_error(pSelf->m_File) ? AVERROR(EIO) : AVERROR_EOF);
		}

		static int64_t Seek(void *pUser, int64_t Offset, int Whence)
		{
			auto *pSelf = static_cast<CDecoder *>(pUser);
			if(pSelf->m_Stop)
				return AVERROR_EXIT;
			Whence &= ~AVSEEK_FORCE;
			if(Whence == AVSEEK_SIZE)
				return pSelf->m_FileSize;
			EIoSeekOrigin Origin;
			switch(Whence)
			{
			case SEEK_SET: Origin = EIoSeekOrigin::START; break;
			case SEEK_CUR: Origin = EIoSeekOrigin::CURRENT; break;
			case SEEK_END: Origin = EIoSeekOrigin::END; break;
			default: return AVERROR(EINVAL);
			}
			return io_seek(pSelf->m_File, Offset, Origin) == 0 ? io_tell(pSelf->m_File) : AVERROR(EIO);
		}

		static int Interrupt(void *pUser)
		{
			return static_cast<CDecoder *>(pUser)->m_Stop.load() ? 1 : 0;
		}

	public:
		AVFormatContext *m_pFormat = nullptr;
		AVCodecContext *m_pCodec = nullptr;
		AVPacket *m_pPacket = nullptr;
		AVFrame *m_pFrame = nullptr;
		AVFrame *m_pNextFrame = nullptr;
		AVFrame *m_pRgba = nullptr;
		SwsContext *m_pScaler = nullptr;
		int m_StreamIndex = -1;
		std::string m_Error;

		explicit CDecoder(std::atomic<bool> &Stop) :
			m_Stop(Stop) {}

		~CDecoder()
		{
			sws_freeContext(m_pScaler);
			av_frame_free(&m_pRgba);
			av_frame_free(&m_pNextFrame);
			av_frame_free(&m_pFrame);
			av_packet_free(&m_pPacket);
			avcodec_free_context(&m_pCodec);
			avformat_close_input(&m_pFormat);
			if(m_pIo)
			{
				av_freep(&m_pIo->buffer);
				avio_context_free(&m_pIo);
			}
			if(m_File)
				io_close(m_File);
		}

		bool Open(IStorage *pStorage, const char *pPath)
		{
			const AVInputFormat *pInputFormat = av_find_input_format("mov");
			if(!pInputFormat)
			{
				m_Error = "当前 FFmpeg 缺少 MP4/MOV 读取器，无法播放视频背景。";
				return false;
			}
			// Native engine I/O supports UTF-8 Windows paths and searches all
			// storage roots for relative paths. FFmpeg never opens a URL here.
			char aResolvedPath[2048];
			m_File = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ALL_OR_ABSOLUTE, aResolvedPath, sizeof(aResolvedPath));
			if(!m_File)
			{
				m_Error = "无法打开本地视频文件：" + std::string(pPath);
				return false;
			}
			m_FileSize = io_length(m_File);
			if(m_FileSize <= 0)
			{
				m_Error = "视频文件为空或无法读取。";
				return false;
			}
			// Start the custom reader at byte zero before avformat_open_input.
			if(io_seek(m_File, 0, EIoSeekOrigin::START) != 0)
			{
				m_Error = "无法回到视频文件开头。";
				return false;
			}
			auto *pBuffer = static_cast<uint8_t *>(av_malloc(32768));
			if(!pBuffer)
			{
				m_Error = "无法分配视频输入缓冲区。";
				return false;
			}
			m_pIo = avio_alloc_context(pBuffer, 32768, 0, this, Read, nullptr, Seek);
			if(!m_pIo)
			{
				av_free(pBuffer);
				m_Error = "无法创建视频输入上下文。";
				return false;
			}
			m_pFormat = avformat_alloc_context();
			if(!m_pFormat)
			{
				m_Error = "无法创建视频容器读取器。";
				return false;
			}
			m_pFormat->pb = m_pIo;
			m_pFormat->flags |= AVFMT_FLAG_CUSTOM_IO;
			m_pFormat->interrupt_callback = {Interrupt, this};
			int Result = avformat_open_input(&m_pFormat, nullptr, pInputFormat, nullptr);
			if(Result < 0)
			{
				m_Error = AvError("无法读取视频容器", Result);
				return false;
			}
			Result = avformat_find_stream_info(m_pFormat, nullptr);
			if(Result < 0)
			{
				m_Error = AvError("无法读取视频流信息", Result);
				return false;
			}
			const AVCodec *pCodec = nullptr;
			m_StreamIndex = av_find_best_stream(m_pFormat, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
			if(m_StreamIndex < 0)
			{
				m_Error = AvError("未找到受支持的视频流", m_StreamIndex);
				return false;
			}
			m_pCodec = avcodec_alloc_context3(pCodec);
			m_pPacket = av_packet_alloc();
			m_pFrame = av_frame_alloc();
			m_pNextFrame = av_frame_alloc();
			if(!m_pCodec || !m_pPacket || !m_pFrame || !m_pNextFrame)
			{
				m_Error = "无法创建视频解码器。";
				return false;
			}
			Result = avcodec_parameters_to_context(m_pCodec, Stream()->codecpar);
			if(Result < 0)
			{
				m_Error = AvError("无法配置视频解码器", Result);
				return false;
			}
			m_pCodec->pkt_timebase = Stream()->time_base;
			// The demuxer need not read audio/subtitle payloads for a silent background.
			for(unsigned i = 0; i < m_pFormat->nb_streams; ++i)
				if(static_cast<int>(i) != m_StreamIndex)
					m_pFormat->streams[i]->discard = AVDISCARD_ALL;
			m_pCodec->thread_count = 2;
			// Give 4K software decoding a little more parallelism without using
			// every core and starving the menu/render thread. Keep small clips
			// on the existing two-thread path.
			const unsigned HardwareThreads = std::thread::hardware_concurrency();
			if(static_cast<int64_t>(m_pCodec->width) * m_pCodec->height > 1920LL * 1080 && HardwareThreads >= 4)
				m_pCodec->thread_count = static_cast<int>(std::min(4U, HardwareThreads / 2));
			m_pCodec->max_pixels = 4096LL * 4096;
			Result = avcodec_open2(m_pCodec, pCodec, nullptr);
			if(Result < 0)
			{
				m_Error = AvError("无法启动视频解码器", Result);
				return false;
			}
			return true;
		}

		AVStream *Stream() const { return m_pFormat->streams[m_StreamIndex]; }

		// 1 = frame, 0 = drained EOF, -1 = error/cancelled.
		int NextFrame(AVFrame *pFrame)
		{
			while(!m_Stop)
			{
				int Result = avcodec_receive_frame(m_pCodec, pFrame);
				if(Result == 0)
					return 1;
				if(Result == AVERROR_EOF)
					return 0;
				if(Result != AVERROR(EAGAIN))
				{
					m_Error = AvError("视频帧解码失败", Result);
					return -1;
				}
				if(m_PacketPending || (m_InputEnded && !m_DrainSent))
				{
					Result = avcodec_send_packet(m_pCodec, m_PacketPending ? m_pPacket : nullptr);
					if(Result < 0)
					{
						m_Error = AvError("无法提交视频数据包", Result);
						return -1;
					}
					if(m_PacketPending)
					{
						av_packet_unref(m_pPacket);
						m_PacketPending = false;
					}
					else
						m_DrainSent = true;
					continue;
				}
				if(m_DrainSent)
				{
					m_Error = "视频解码器未能排空末尾帧。";
					return -1;
				}
				Result = av_read_frame(m_pFormat, m_pPacket);
				if(Result == AVERROR_EOF)
					m_InputEnded = true;
				else if(Result < 0)
				{
					m_Error = AvError("视频数据包读取失败", Result);
					return -1;
				}
				else if(m_pPacket->stream_index == m_StreamIndex)
					m_PacketPending = true;
				else
					av_packet_unref(m_pPacket);
			}
			return -1;
		}

		bool Rewind()
		{
			const int64_t Start = Stream()->start_time == AV_NOPTS_VALUE ? 0 : Stream()->start_time;
			const int Result = av_seek_frame(m_pFormat, m_StreamIndex, Start, AVSEEK_FLAG_BACKWARD);
			if(Result < 0)
			{
				m_Error = AvError("无法跳转到视频开头进行循环", Result);
				return false;
			}
			avcodec_flush_buffers(m_pCodec);
			av_packet_unref(m_pPacket);
			av_frame_unref(m_pFrame);
			av_frame_unref(m_pNextFrame);
			m_PacketPending = m_InputEnded = m_DrainSent = false;
			return true;
		}

		bool Convert(CImageInfo &Image, float &Aspect, int ScreenWidth, int ScreenHeight)
		{
			const int Width = m_pFrame->width;
			const int Height = m_pFrame->height;
			if(Width <= 0 || Height <= 0 || Width > 8192 || Height > 8192 || static_cast<int64_t>(Width) * Height > 4096LL * 4096)
			{
				m_Error = "视频尺寸超出解码限制（最大 1677 万像素，单边最大 8192）。";
				return false;
			}
			const AVRational SampleAspect = av_guess_sample_aspect_ratio(m_pFormat, Stream(), m_pFrame);
			const double PixelAspect = SampleAspect.num > 0 && SampleAspect.den > 0 ? av_q2d(SampleAspect) : 1.0;
			Aspect = static_cast<float>(static_cast<double>(Width) / Height * PixelAspect);
			if(!std::isfinite(Aspect) || Aspect <= 0.0f)
				Aspect = static_cast<float>(Width) / Height;
			// Cover the actual viewport without converting pixels that will only
			// be downscaled again. Preserve the existing 1920-pixel safety limit.
			double Scale = std::min(1.0, 1920.0 / std::max(Width, Height));
			if(ScreenWidth > 0 && ScreenHeight > 0)
				Scale = std::min(Scale, std::max(ScreenWidth / (Height * static_cast<double>(Aspect)), static_cast<double>(ScreenHeight) / Height));
			const int OutputWidth = std::max(1, static_cast<int>(Width * Scale));
			const int OutputHeight = std::max(1, static_cast<int>(Height * Scale));
			m_pScaler = sws_getCachedContext(m_pScaler, Width, Height, static_cast<AVPixelFormat>(m_pFrame->format), OutputWidth, OutputHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
			if(!m_pScaler)
			{
				m_Error = "无法将视频像素转换为 RGBA。";
				return false;
			}
			// An image skipped after conversion can be reused. An uploaded image
			// is moved to the graphics queue and must never be overwritten here.
			if(!Image.m_pData || Image.m_Width != static_cast<size_t>(OutputWidth) || Image.m_Height != static_cast<size_t>(OutputHeight))
			{
				Image.Free();
				Image.m_Width = OutputWidth;
				Image.m_Height = OutputHeight;
				Image.m_Format = CImageInfo::FORMAT_RGBA;
				Image.Allocate();
			}
			if(!Image.m_pData)
			{
				m_Error = "无法分配视频帧像素缓冲区。";
				return false;
			}
			// SIMD conversion needs padded, aligned rows at arbitrary viewport sizes.
			if(!m_pRgba || m_pRgba->width != OutputWidth || m_pRgba->height != OutputHeight)
			{
				av_frame_free(&m_pRgba);
				m_pRgba = av_frame_alloc();
				if(!m_pRgba)
					return false;
				m_pRgba->format = AV_PIX_FMT_RGBA;
				m_pRgba->width = OutputWidth;
				m_pRgba->height = OutputHeight;
				if(av_frame_get_buffer(m_pRgba, 32) < 0)
				{
					av_frame_free(&m_pRgba);
					m_Error = "无法分配视频转换缓冲区。";
					return false;
				}
			}
			if(sws_scale(m_pScaler, m_pFrame->data, m_pFrame->linesize, 0, Height, m_pRgba->data, m_pRgba->linesize) != OutputHeight)
			{
				m_Error = "视频帧像素转换不完整。";
				return false;
			}
			for(int Row = 0; Row < OutputHeight; ++Row)
				mem_copy(Image.m_pData + static_cast<size_t>(Row) * OutputWidth * 4, m_pRgba->data[0] + static_cast<size_t>(Row) * m_pRgba->linesize[0], OutputWidth * 4);
			return true;
		}
	};
}
#endif

class CVideoBackground::CImpl
{
public:
	using TClock = std::chrono::steady_clock;
	enum class EState
	{
		DISABLED,
		LOADING,
		PLAYING,
		ERROR,
	};

	IGraphics::CTextureHandle m_Texture;
	size_t m_TextureWidth = 0;
	size_t m_TextureHeight = 0;
	float m_Aspect = 1.0f;
	bool m_RequestInitialized = false;
	bool m_Enabled = false;
	std::string m_Path;
	mutable std::mutex m_Mutex;
	std::condition_variable m_Condition;
	std::atomic<bool> m_Stop{false};
	std::thread m_Thread;
	EState m_State = EState::DISABLED;
	std::string m_Status = "视频背景已关闭。";
	CImageInfo m_LatestFrame;
	float m_LatestAspect = 1.0f;
	TClock::time_point m_LatestDeadline;
	TClock::time_point m_LastRender = TClock::now();
	bool m_HasRendered = false;
	double m_RenderInterval = 1.0 / 60.0;
	TClock::duration m_PlaybackOffset{};
	int m_ScreenWidth = 0;
	int m_ScreenHeight = 0;
	size_t m_OutputWidth = 0;
	size_t m_OutputHeight = 0;
	double m_SourceFps = 0.0;

	struct CPerformance
	{
		uint64_t m_Decoded = 0;
		uint64_t m_Converted = 0;
		uint64_t m_SkippedLate = 0;
		uint64_t m_SkippedCadence = 0;
		uint64_t m_Created = 0;
		uint64_t m_Updated = 0;
		uint64_t m_UploadBytes = 0;
		uint64_t m_Rebases = 0;
		uint64_t m_Loops = 0;
		double m_DecodeSeconds = 0.0;
		double m_DecodeMax = 0.0;
		double m_ConvertSeconds = 0.0;
		double m_ConvertMax = 0.0;
		double m_EnqueueSeconds = 0.0;
		double m_EnqueueMax = 0.0;
		double m_LateSeconds = 0.0;
		double m_LateMax = 0.0;
	};
	CPerformance m_Performance;
	TClock::time_point m_PerformanceStart = TClock::now();

	static TClock::duration Seconds(double Value)
	{
		return std::chrono::duration_cast<TClock::duration>(std::chrono::duration<double>(Value));
	}

	static double Elapsed(TClock::duration Value)
	{
		return std::chrono::duration<double>(Value).count();
	}

	// Requires m_Mutex. Slow renderers must not look like hidden menus.
	TClock::duration IdleTimeout() const { return Seconds(std::max(0.25, 3.0 * m_RenderInterval)); }

	void ReportPerformance(bool Force = false)
	{
		CPerformance Performance;
		double Interval;
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			const auto Now = TClock::now();
			Interval = Elapsed(Now - m_PerformanceStart);
			if((!Force && Interval < 5.0) || (m_Performance.m_Decoded == 0 && m_Performance.m_Created == 0 && m_Performance.m_Updated == 0))
				return;
			Performance = m_Performance;
			m_Performance = {};
			m_PerformanceStart = Now;
			if(m_State == EState::PLAYING && Interval > 0.0)
			{
				char aStatus[256];
				std::snprintf(aStatus, sizeof(aStatus), "正在循环播放本地视频（无音频）｜%zux%zu｜源 %.2f FPS｜转换 %.1f FPS｜更新 %.1f FPS",
					m_OutputWidth, m_OutputHeight, m_SourceFps, Performance.m_Converted / Interval, (Performance.m_Created + Performance.m_Updated) / Interval);
				m_Status = aStatus;
			}
		}
		const auto AverageMs = [](double Total, uint64_t Count) { return Count ? Total * 1000.0 / Count : 0.0; };
		const uint64_t Uploaded = Performance.m_Created + Performance.m_Updated;
		// Enqueue timings cover CPU command submission, not asynchronous GPU
		// completion. Compare these counters with the renderer's frame times.
		log_info("video_background/perf", "%.2fs decode=%llu avg/max=%.3f/%.3fms convert=%llu avg/max=%.3f/%.3fms skip(late/cadence)=%llu/%llu upload(create/update)=%llu/%llu %.2fMiB enqueue avg/max=%.3f/%.3fms pts_late avg/max=%.3f/%.3fms loops=%llu rebases=%llu",
			Interval,
			static_cast<unsigned long long>(Performance.m_Decoded), AverageMs(Performance.m_DecodeSeconds, Performance.m_Decoded), Performance.m_DecodeMax * 1000.0,
			static_cast<unsigned long long>(Performance.m_Converted), AverageMs(Performance.m_ConvertSeconds, Performance.m_Converted), Performance.m_ConvertMax * 1000.0,
			static_cast<unsigned long long>(Performance.m_SkippedLate), static_cast<unsigned long long>(Performance.m_SkippedCadence),
			static_cast<unsigned long long>(Performance.m_Created), static_cast<unsigned long long>(Performance.m_Updated), Performance.m_UploadBytes / (1024.0 * 1024.0),
			AverageMs(Performance.m_EnqueueSeconds, Uploaded), Performance.m_EnqueueMax * 1000.0,
			AverageMs(Performance.m_LateSeconds, Uploaded), Performance.m_LateMax * 1000.0,
			static_cast<unsigned long long>(Performance.m_Loops), static_cast<unsigned long long>(Performance.m_Rebases));
	}

	~CImpl() { Stop(); }

	void Stop()
	{
		{
			// Synchronize the stop predicate with condition-variable waiters
			// to prevent a notification being lost just before they sleep.
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_Stop = true;
		}
		m_Condition.notify_all();
		if(m_Thread.joinable())
			m_Thread.join();
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_LatestFrame.Free();
			m_HasRendered = false;
			m_RenderInterval = 1.0 / 60.0;
			m_PlaybackOffset = {};
		}
		ReportPerformance(true);
	}

	void SetStatus(EState State, const std::string &Status)
	{
		std::lock_guard<std::mutex> Lock(m_Mutex);
		m_State = State;
		m_Status = Status;
	}

	void Fail(const std::string &Status)
	{
		if(m_Stop)
			return;
		SetStatus(EState::ERROR, Status);
		log_warn("video_background", "%s", Status.c_str());
	}

#if defined(CONF_VIDEORECORDER)
	// Requires m_Mutex. The render thread records a pause once on resume;
	// every worker deadline uses the same offset, including a prepared frame.
	void SyncClock(TClock::time_point &Epoch, TClock::duration &AppliedOffset)
	{
		Epoch += m_PlaybackOffset - AppliedOffset;
		AppliedOffset = m_PlaybackOffset;
	}

	bool WaitForSlot(TClock::time_point &Epoch, TClock::duration &AppliedOffset)
	{
		std::unique_lock<std::mutex> Lock(m_Mutex);
		while(!m_Stop)
		{
			SyncClock(Epoch, AppliedOffset);
			const auto Now = TClock::now();
			const auto IdleDeadline = m_LastRender + IdleTimeout();
			// Keep one frame prepared. During normal playback wait for the
			// renderer to consume it, or until its presentation deadline. The
			// old code waited for IdleDeadline here, throttling 60 FPS video to
			// roughly 20 FPS because IdleDeadline is intentionally 3 frames long.
			if(m_LatestFrame.m_pData)
			{
				if(Now < m_LatestDeadline)
					m_Condition.wait_until(Lock, m_LatestDeadline);
				else
					m_Condition.wait(Lock);
			}
			else if(Now < IdleDeadline)
				return true;
			else
				m_Condition.wait(Lock);
		}
		return false;
	}

	int DecodeFrame(CDecoder &Decoder, AVFrame *pFrame)
	{
		const auto Start = TClock::now();
		const int Result = Decoder.NextFrame(pFrame);
		const double Cost = Elapsed(TClock::now() - Start);
		if(Result == 1)
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			++m_Performance.m_Decoded;
			m_Performance.m_DecodeSeconds += Cost;
			m_Performance.m_DecodeMax = std::max(m_Performance.m_DecodeMax, Cost);
		}
		return Result;
	}

	void Run(IStorage *pStorage, const char *pPath)
	{
		CDecoder Decoder(m_Stop);
		if(!Decoder.Open(pStorage, pPath))
		{
			Fail(Decoder.m_Error);
			return;
		}
		const AVRational Rate = av_guess_frame_rate(Decoder.m_pFormat, Decoder.Stream(), nullptr);
		const double FrameDuration = Rate.num > 0 && Rate.den > 0 ? std::clamp(1.0 / av_q2d(Rate), 0.001, 60.0) : 1.0 / 30.0;
		{
			std::lock_guard<std::mutex> Lock(m_Mutex);
			m_SourceFps = 1.0 / FrameDuration;
		}
		log_info("video_background", "decoder=%s input=%dx%d source_fps=%.3f threads=%d", Decoder.m_pCodec->codec->name, Decoder.m_pCodec->width, Decoder.m_pCodec->height, 1.0 / FrameDuration, Decoder.m_pCodec->thread_count);
		auto Epoch = TClock::now();
		TClock::duration AppliedOffset{};
		double Presentation = 0.0;
		double NextSample = 0.0;
		bool TimelineStarted = false;
		bool HasFrame = false;
		CImageInfo Image;
		while(!m_Stop)
		{
			if(!WaitForSlot(Epoch, AppliedOffset))
				return;
			if(!HasFrame)
			{
				const int Result = DecodeFrame(Decoder, Decoder.m_pFrame);
				if(Result != 1)
				{
					Fail(Result < 0 ? Decoder.m_Error : "视频流中没有可以解码的画面。");
					return;
				}
			}
			// Decode one frame ahead to know exactly when the current frame
			// expires (also for VFR). Referenced codec frames are still decoded;
			// only unnecessary RGBA conversion/upload is skipped.
			const int NextResult = DecodeFrame(Decoder, Decoder.m_pNextFrame);
			if(NextResult < 0)
			{
				Fail(Decoder.m_Error);
				return;
			}
#if LIBAVUTIL_VERSION_MAJOR >= 58
			const int64_t RawDuration = Decoder.m_pFrame->duration;
#else
			const int64_t RawDuration = Decoder.m_pFrame->pkt_duration;
#endif
			const double TimeBase = av_q2d(Decoder.Stream()->time_base);
			const double TimestampDuration = RawDuration * TimeBase;
			double Duration = std::isfinite(TimestampDuration) && TimestampDuration > 0.0 && TimestampDuration <= 60.0 ? TimestampDuration : FrameDuration;
			if(NextResult == 1)
			{
				const int64_t Timestamp = Decoder.m_pFrame->best_effort_timestamp;
				const int64_t NextTimestamp = Decoder.m_pNextFrame->best_effort_timestamp;
				if(Timestamp != AV_NOPTS_VALUE && NextTimestamp != AV_NOPTS_VALUE)
				{
					const double Difference = (static_cast<double>(NextTimestamp) - static_cast<double>(Timestamp)) * TimeBase;
					if(std::isfinite(Difference) && Difference > 0.0 && Difference <= 60.0)
						Duration = Difference;
				}
			}
			bool Skip;
			int ScreenWidth, ScreenHeight;
			{
				std::lock_guard<std::mutex> Lock(m_Mutex);
				SyncClock(Epoch, AppliedOffset);
				const auto Now = TClock::now();
				const auto Deadline = Epoch + Seconds(Presentation);
				// Bound catch-up after a severe decode stall. Ordinary lateness
				// skips expired frames without changing the playback speed.
				if(TimelineStarted && Now - Deadline > std::chrono::seconds(1))
				{
					Epoch = Now - Seconds(Presentation);
					NextSample = Presentation;
					++m_Performance.m_Rebases;
				}
				const bool Late = TimelineStarted && Epoch + Seconds(Presentation + Duration) <= Now;
				const bool Cadence = TimelineStarted && Presentation + Duration <= NextSample + 0.000001;
				Skip = Late || Cadence;
				if(Late)
					++m_Performance.m_SkippedLate;
				else if(Cadence)
					++m_Performance.m_SkippedCadence;
				ScreenWidth = m_ScreenWidth;
				ScreenHeight = m_ScreenHeight;
			}
			if(!Skip)
			{
				float Aspect;
				const auto Start = TClock::now();
				if(!Decoder.Convert(Image, Aspect, ScreenWidth, ScreenHeight))
				{
					Fail(Decoder.m_Error);
					return;
				}
				const double Cost = Elapsed(TClock::now() - Start);
				std::lock_guard<std::mutex> Lock(m_Mutex);
				if(m_Stop)
					return;
				const auto Now = TClock::now();
				++m_Performance.m_Converted;
				m_Performance.m_ConvertSeconds += Cost;
				m_Performance.m_ConvertMax = std::max(m_Performance.m_ConvertMax, Cost);
				SyncClock(Epoch, AppliedOffset);
				if(!TimelineStarted)
				{
					// Start playback when the first pixels are ready, not before
					// probing/decoding/converting the first pair of frames.
					Epoch = Now - Seconds(Presentation);
					TimelineStarted = true;
				}
				if(Epoch + Seconds(Presentation + Duration) <= Now)
					++m_Performance.m_SkippedLate;
				else
				{
					// Publish before the PTS deadline. Render only takes due frames,
					// so conversion time and worker wake-up jitter do not delay PTS.
					m_LatestFrame = std::move(Image);
					m_LatestAspect = Aspect;
					m_LatestDeadline = Epoch + Seconds(Presentation);
					NextSample = std::max(NextSample, Presentation) + m_RenderInterval;
					if(m_State != EState::PLAYING)
					{
						m_State = EState::PLAYING;
						char aStatus[192];
						std::snprintf(aStatus, sizeof(aStatus), "正在循环播放本地视频（无音频）｜%zux%zu｜源 %.2f FPS", m_LatestFrame.m_Width, m_LatestFrame.m_Height, m_SourceFps);
						m_Status = aStatus;
					}
				}
			}
			av_frame_unref(Decoder.m_pFrame);
			if(NextResult == 1)
			{
				std::swap(Decoder.m_pFrame, Decoder.m_pNextFrame);
				Presentation += Duration;
				HasFrame = true;
			}
			else
			{
				// Keep loop timestamps continuous and retain the last texture
				// until the first frame of the next loop is due.
				const double LoopDuration = Presentation + Duration;
				Epoch += Seconds(LoopDuration);
				NextSample = std::max(0.0, NextSample - LoopDuration);
				Presentation = 0.0;
				HasFrame = false;
				if(!Decoder.Rewind())
				{
					Fail(Decoder.m_Error);
					return;
				}
				std::lock_guard<std::mutex> Lock(m_Mutex);
				++m_Performance.m_Loops;
			}
		}
	}
#endif
};

CVideoBackground::CVideoBackground() :
	m_pImpl(std::make_unique<CImpl>())
{
}

CVideoBackground::~CVideoBackground() = default;

bool CVideoBackground::IsSupported()
{
#if defined(CONF_VIDEORECORDER)
	static const bool Supported = [] {
		if(!av_find_input_format("mov"))
			return false;
		void *pIterator = nullptr;
		while(const AVCodec *pCodec = av_codec_iterate(&pIterator))
		{
			if(pCodec->type == AVMEDIA_TYPE_VIDEO && av_codec_is_decoder(pCodec))
				return true;
		}
		return false;
	}();
	return Supported;
#else
	return false;
#endif
}

void CVideoBackground::Shutdown(IGraphics *pGraphics)
{
	m_pImpl->Stop();
	if(m_pImpl->m_Texture.IsValid())
		pGraphics->UnloadTexture(&m_pImpl->m_Texture);
	m_pImpl->m_TextureWidth = m_pImpl->m_TextureHeight = 0;
	m_pImpl->m_RequestInitialized = false;
	m_pImpl->SetStatus(CImpl::EState::DISABLED, "视频背景已关闭。");
}

std::string CVideoBackground::GetStatus() const
{
	std::lock_guard<std::mutex> Lock(m_pImpl->m_Mutex);
	return m_pImpl->m_Status;
}

bool CVideoBackground::Render(IGraphics *pGraphics, IStorage *pStorage, bool Enabled, const char *pPath, int Dim)
{
#if !defined(CONF_VIDEORECORDER)
	(void)pStorage;
#endif
	CImpl &Impl = *m_pImpl;
	if(!Impl.m_RequestInitialized || Enabled != Impl.m_Enabled || Impl.m_Path != pPath)
	{
		Shutdown(pGraphics);
		Impl.m_RequestInitialized = true;
		Impl.m_Enabled = Enabled;
		Impl.m_Path = pPath;
		Impl.m_Stop = false;
		{
			std::lock_guard<std::mutex> Lock(Impl.m_Mutex);
			Impl.m_LastRender = CImpl::TClock::now();
			Impl.m_PerformanceStart = Impl.m_LastRender;
			Impl.m_ScreenWidth = pGraphics->ScreenWidth();
			Impl.m_ScreenHeight = pGraphics->ScreenHeight();
			Impl.m_OutputWidth = Impl.m_OutputHeight = 0;
			Impl.m_SourceFps = 0.0;
		}
		if(Enabled)
		{
			if(!IsSupported())
			{
#if defined(CONF_VIDEORECORDER)
				Impl.Fail("当前 FFmpeg 缺少 MP4 读取器或视频解码器；请使用支持 MP4/H.264 播放的 FFmpeg DLL。");
#else
				Impl.Fail("此版本未启用 FFmpeg，无法播放视频背景；需要启用 CONF_VIDEORECORDER 后重新编译。");
#endif
			}
			else if(!pPath[0])
				Impl.Fail("请设置本地 MP4 视频文件路径。");
#if defined(CONF_VIDEORECORDER)
			else
			{
				Impl.SetStatus(CImpl::EState::LOADING, "正在打开并解码本地视频……");
				try
				{
					Impl.m_Thread = std::thread([&Impl, pStorage, Path = Impl.m_Path] {
						try
						{
							Impl.Run(pStorage, Path.c_str());
						}
						catch(const std::exception &Error)
						{
							Impl.Fail(std::string("视频播放失败：") + Error.what());
						}
					});
				}
				catch(const std::exception &Error)
				{
					Impl.Fail(std::string("无法启动视频解码线程：") + Error.what());
				}
			}
#endif
		}
	}
	if(!Enabled)
		return false;

	CImageInfo Frame;
	float Aspect = 1.0f;
	CImpl::TClock::time_point Deadline;
	bool Failed;
	bool WakeDecoder = false;
	{
		std::lock_guard<std::mutex> Lock(Impl.m_Mutex);
		const auto Now = CImpl::TClock::now();
		if(Impl.m_HasRendered)
		{
			const auto Gap = Now - Impl.m_LastRender;
			if(Gap >= Impl.IdleTimeout())
			{
				// Freeze the media clock while hidden. Shift a queued future
				// frame as well, so resume cannot consume it ahead of its PTS.
				const auto Paused = Gap - CImpl::Seconds(Impl.m_RenderInterval);
				Impl.m_PlaybackOffset += Paused;
				if(Impl.m_LatestFrame.m_pData)
					Impl.m_LatestDeadline += Paused;
				WakeDecoder = true;
			}
			else if(Gap >= std::chrono::microseconds(500))
				Impl.m_RenderInterval = std::clamp(0.8 * Impl.m_RenderInterval + 0.2 * CImpl::Elapsed(Gap), 0.001, 1.0);
		}
		Impl.m_HasRendered = true;
		Impl.m_LastRender = Now;
		Impl.m_ScreenWidth = pGraphics->ScreenWidth();
		Impl.m_ScreenHeight = pGraphics->ScreenHeight();
		Failed = Impl.m_State == CImpl::EState::ERROR;
		if(!Failed && Impl.m_LatestFrame.m_pData && Impl.m_LatestDeadline <= Now)
		{
			Frame = std::move(Impl.m_LatestFrame);
			Aspect = Impl.m_LatestAspect;
			Deadline = Impl.m_LatestDeadline;
			WakeDecoder = true;
		}
	}
	if(WakeDecoder)
		Impl.m_Condition.notify_all();
	Impl.ReportPerformance();
	if(Failed)
	{
		if(Impl.m_Texture.IsValid())
			pGraphics->UnloadTexture(&Impl.m_Texture);
		return false;
	}
	if(Frame.m_pData)
	{
		const auto Start = CImpl::TClock::now();
		const size_t Width = Frame.m_Width;
		const size_t Height = Frame.m_Height;
		const size_t Bytes = Frame.DataSize();
		const bool Create = !Impl.m_Texture.IsValid() || Impl.m_TextureWidth != Width || Impl.m_TextureHeight != Height;
		bool Uploaded;
		if(Create)
		{
			// Only the first frame or a size change allocates a GPU texture.
			const auto Texture = pGraphics->LoadTextureRawMove(Frame, IGraphics::TEXLOAD_NO_MIPMAPS, "xyrion-video-background");
			Uploaded = Texture.IsValid() && !Texture.IsNullTexture();
			if(Uploaded)
			{
				if(Impl.m_Texture.IsValid())
					pGraphics->UnloadTexture(&Impl.m_Texture);
				Impl.m_Texture = Texture;
				Impl.m_TextureWidth = Width;
				Impl.m_TextureHeight = Height;
			}
		}
		else
		{
			// Ownership moves to the asynchronous graphics queue. Neither the
			// decoder nor the main thread may reuse these pixels afterwards.
			Uploaded = pGraphics->UpdateTextureRawMove(Impl.m_Texture, Frame);
		}
		if(!Uploaded)
		{
			{
				std::lock_guard<std::mutex> Lock(Impl.m_Mutex);
				Impl.m_Stop = true;
				Impl.m_State = CImpl::EState::ERROR;
				Impl.m_Status = "无法上传视频背景纹理。";
			}
			Impl.m_Condition.notify_all();
			log_warn("video_background", "无法上传视频背景纹理。");
			if(Impl.m_Texture.IsValid())
				pGraphics->UnloadTexture(&Impl.m_Texture);
			return false;
		}
		Impl.m_Aspect = Aspect;
		const double Cost = CImpl::Elapsed(CImpl::TClock::now() - Start);
		const double Late = std::max(0.0, CImpl::Elapsed(Start - Deadline));
		std::lock_guard<std::mutex> Lock(Impl.m_Mutex);
		Impl.m_OutputWidth = Width;
		Impl.m_OutputHeight = Height;
		if(Create)
			++Impl.m_Performance.m_Created;
		else
			++Impl.m_Performance.m_Updated;
		Impl.m_Performance.m_UploadBytes += Bytes;
		Impl.m_Performance.m_EnqueueSeconds += Cost;
		Impl.m_Performance.m_EnqueueMax = std::max(Impl.m_Performance.m_EnqueueMax, Cost);
		Impl.m_Performance.m_LateSeconds += Late;
		Impl.m_Performance.m_LateMax = std::max(Impl.m_Performance.m_LateMax, Late);
	}
	if(!Impl.m_Texture.IsValid())
		return false;

	const auto PreviousScreen = pGraphics->GetScreen();
	const float ScreenAspect = pGraphics->ScreenAspect();
	if(!std::isfinite(ScreenAspect) || ScreenAspect <= 0.0f)
		return false;
	pGraphics->MapScreenToSize(ScreenAspect, 1.0f);
	pGraphics->BlendNormal();
	pGraphics->WrapClamp();
	pGraphics->TextureSet(Impl.m_Texture);
	pGraphics->QuadsBegin();
	const float Brightness = 1.0f - std::clamp(Dim, 0, 100) / 100.0f;
	pGraphics->SetColor(Brightness, Brightness, Brightness, 1.0f);
	// Fill the viewport without stretching; crop equally at opposite edges.
	const float VisibleWidth = std::min(1.0f, ScreenAspect / Impl.m_Aspect);
	const float VisibleHeight = std::min(1.0f, Impl.m_Aspect / ScreenAspect);
	pGraphics->QuadsSetSubset((1.0f - VisibleWidth) / 2.0f, (1.0f - VisibleHeight) / 2.0f, (1.0f + VisibleWidth) / 2.0f, (1.0f + VisibleHeight) / 2.0f);
	const IGraphics::CQuadItem Quad(0.0f, 0.0f, ScreenAspect, 1.0f);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->TextureClear();
	pGraphics->WrapNormal();
	pGraphics->MapScreen(PreviousScreen);
	return true;
}
