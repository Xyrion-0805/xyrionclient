// QmClient 系统媒体接口的 MinGW 适配。HUD 的外观和动画由 Qm 原版负责。
#include "system_media_controls.h"
#include <game/client/gameclient.h>

#include <base/str.h>
#include <base/time.h>
#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>
#include <windows.h>
#endif

#include <engine/http.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <game/client/components/bestclient/media_decoder.h>
#include <game/client/components/qmclient/netease/netease_lyric_parser.h>
#include <game/client/components/qmclient/netease/netease_lyric_timeline.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace
{
const char *JsonString(const json_value *pObject, const char *pKey)
{
	const char *pText = json_string_get(json_object_get(pObject, pKey));
	return pText ? pText : "";
}

// 子进程和管道只由采集线程持有，游戏帧不等待 WinRT、JSON 或图片解码。
class CMediaBridge
{
#if defined(CONF_FAMILY_WINDOWS)
	HANDLE m_Process = nullptr;
	HANDLE m_Output = nullptr;
	HANDLE m_Input = nullptr;
#endif
	std::string m_Buffer;

public:
	explicit CMediaBridge(const char *pScript)
	{
#if defined(CONF_FAMILY_WINDOWS)
		if(!pScript || !pScript[0]) return;
		SECURITY_ATTRIBUTES Security{sizeof(Security), nullptr, TRUE};
		HANDLE ChildOutput = nullptr, ChildInput = nullptr;
		if(!CreatePipe(&m_Output, &ChildOutput, &Security, 0)) return;
		if(!CreatePipe(&ChildInput, &m_Input, &Security, 0))
		{
			CloseHandle(ChildOutput);
			return;
		}
		SetHandleInformation(m_Output, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(m_Input, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW Startup{};
		Startup.cb = sizeof(Startup);
		Startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
		Startup.wShowWindow = SW_HIDE;
		Startup.hStdOutput = ChildOutput;
		Startup.hStdError = ChildOutput;
		Startup.hStdInput = ChildInput;
		PROCESS_INFORMATION Process{};
		wchar_t aWindows[MAX_PATH];
		GetSystemDirectoryW(aWindows, MAX_PATH);
		const std::wstring Application = std::wstring(aWindows) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
		std::wstring Command = L"\"" + Application + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + windows_utf8_to_wide(pScript) + L"\" -ParentId " + std::to_wstring(GetCurrentProcessId());
		if(CreateProcessW(Application.c_str(), Command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &Startup, &Process))
		{
			m_Process = Process.hProcess;
			CloseHandle(Process.hThread);
		}
		CloseHandle(ChildInput);
		CloseHandle(ChildOutput);
#endif
	}
	~CMediaBridge()
	{
#if defined(CONF_FAMILY_WINDOWS)
		if(m_Input) CloseHandle(m_Input);
		if(m_Output) CloseHandle(m_Output);
		if(m_Process)
		{
			if(WaitForSingleObject(m_Process, 0) == WAIT_TIMEOUT) TerminateProcess(m_Process, 0);
			CloseHandle(m_Process);
		}
#endif
	}
	bool Alive() const
	{
#if defined(CONF_FAMILY_WINDOWS)
		return m_Process && WaitForSingleObject(m_Process, 0) == WAIT_TIMEOUT;
#else
		return false;
#endif
	}
	void Control(const std::string &Command)
	{
#if defined(CONF_FAMILY_WINDOWS)
		if(m_Input && Alive())
		{
			DWORD Written = 0;
			WriteFile(m_Input, Command.data(), Command.size(), &Written, nullptr);
		}
#endif
	}
	std::string ReadLatest()
	{
#if defined(CONF_FAMILY_WINDOWS)
		DWORD Available = 0, Read = 0;
		char aRead[16384];
		for(int i = 0; i < 128 && m_Output && PeekNamedPipe(m_Output, nullptr, 0, nullptr, &Available, nullptr) && Available; ++i)
		{
			if(!ReadFile(m_Output, aRead, std::min<DWORD>(Available, sizeof(aRead)), &Read, nullptr) || !Read) break;
			m_Buffer.append(aRead, Read);
			if(m_Buffer.size() > 12 * 1024 * 1024) { m_Buffer.clear(); break; }
		}
#endif
		std::string Latest;
		size_t End;
		while((End = m_Buffer.find('\n')) != std::string::npos)
		{
			if(!m_Buffer.empty() && m_Buffer[0] == '{') Latest.assign(m_Buffer, 0, End);
			m_Buffer.erase(0, End + 1);
		}
		return Latest;
	}
};

struct SPreparedCover
{
	CImageInfo m_Original;
	CImageInfo m_Circular;
};

std::unique_ptr<SPreparedCover> DecodeCover(IGraphics *pGraphics, const std::string &Encoded)
{
	if(Encoded.empty() || Encoded.size() > 12 * 1024 * 1024) return nullptr;
	std::vector<unsigned char> vBytes(Encoded.size() * 3 / 4 + 3);
	const int Size = str_base64_decode(vBytes.data(), vBytes.size(), Encoded.c_str());
	if(Size <= 0) return nullptr;
	SMediaDecodedFrames Frames;
	if(!MediaDecoder::DecodeStaticImageCpu(pGraphics, vBytes.data(), Size, "qm_media_cover", Frames, SystemMediaControls::ALBUM_ART_MAX_DIMENSION))
	{
		SMediaDecodeLimits Limits;
		Limits.m_MaxDimension = SystemMediaControls::ALBUM_ART_MAX_DIMENSION;
		Limits.m_MaxFrames = 1;
		Limits.m_MaxTotalBytes = 4 * 1024 * 1024;
		if(!MediaDecoder::DecodeImageWithFfmpegCpu(pGraphics, vBytes.data(), Size, "qm_media_cover", Frames, Limits)) return nullptr;
	}
	if(Frames.Empty()) return nullptr;
	auto pCover = std::make_unique<SPreparedCover>();
	pCover->m_Original = std::move(Frames.m_vFrames[0].m_Image);
	auto &Original = pCover->m_Original;
	auto &Circular = pCover->m_Circular;
	if(Original.m_Format != CImageInfo::FORMAT_RGBA) return nullptr;
	Circular.m_Width = Original.m_Width;
	Circular.m_Height = Original.m_Height;
	Circular.m_Format = CImageInfo::FORMAT_RGBA;
	Circular.Allocate();
	std::copy_n(Original.m_pData, Original.m_Width * Original.m_Height * 4, Circular.m_pData);
	const float Feather = std::clamp((float)std::min(Original.m_Width, Original.m_Height) / 64.0f, 1.0f, 6.0f);
	for(size_t y = 0; y < Circular.m_Height; ++y)
		for(size_t x = 0; x < Circular.m_Width; ++x)
		{
			const size_t Index = (y * Circular.m_Width + x) * 4 + 3;
			Circular.m_pData[Index] = (uint8_t)std::round(Circular.m_pData[Index] * SystemMediaControls::AlbumArtCircleMaskAlpha(x + 0.5f, y + 0.5f, Circular.m_Width, Circular.m_Height, Feather));
		}
	return pCover;
}
}

class CSystemMediaControls::CImpl
{
public:
	SState m_State;
	SState m_PendingState;
	std::mutex m_Mutex;
	std::condition_variable m_Wake;
	std::atomic_bool m_Stop{false};
	std::thread m_Thread;
	std::deque<std::string> m_Commands;
	std::string m_PendingCoverKey, m_AppliedCoverKey;
	std::unique_ptr<SPreparedCover> m_pPendingCover;
	struct SRetiredCover { IGraphics::CTextureHandle m_Texture; int64_t m_ReleaseTick; };
	std::vector<SRetiredCover> m_vRetiredCovers;
	NeteaseLyrics::STimeline m_Lyrics;
	std::shared_ptr<IHttpRequest> m_pLyricsRequest;
	std::string m_LyricsKey;
	int64_t m_LyricsRetryTick = 0;

	~CImpl()
	{
		m_Stop = true;
		m_Wake.notify_all();
		if(m_Thread.joinable()) m_Thread.join();
		if(m_pLyricsRequest) m_pLyricsRequest->Abort();
	}
	void Run(std::string Script, IGraphics *pGraphics)
	{
		std::string PreviousEncoded;
		std::string PreviousTrack;
		int64_t RetryTick = 0;
		std::unique_ptr<CMediaBridge> pBridge;
		while(!m_Stop)
		{
			if((!pBridge || !pBridge->Alive()) && time_get_impl() >= RetryTick)
			{
				pBridge = std::make_unique<CMediaBridge>(Script.c_str());
				RetryTick = time_get_impl() + 10 * time_freq();
			}
			std::deque<std::string> Commands;
			{
				std::lock_guard Lock(m_Mutex);
				Commands.swap(m_Commands);
			}
			for(const auto &Command : Commands) pBridge->Control(Command);
			const std::string Line = pBridge->ReadLatest();
			if(!Line.empty())
			{
				json_value *pRoot = JsonParse(Line.data(), Line.size());
				if(pRoot && pRoot->type == json_object)
				{
					SState State;
					str_copy(State.m_aTitle, JsonString(pRoot, "title"));
					str_copy(State.m_aArtist, JsonString(pRoot, "artist"));
					str_copy(State.m_aAlbum, JsonString(pRoot, "album"));
					str_copy(State.m_aSourceAppId, JsonString(pRoot, "app"));
					State.m_Playing = json_boolean_get(json_object_get(pRoot, "playing"));
					State.m_PlaybackState = State.m_Playing ? EPlaybackState::Playing : EPlaybackState::Paused;
					State.m_CanPlay = State.m_CanPause = json_boolean_get(json_object_get(pRoot, "playpause"));
					State.m_CanPrev = json_boolean_get(json_object_get(pRoot, "previous"));
					State.m_CanNext = json_boolean_get(json_object_get(pRoot, "next"));
					State.m_PositionMs = std::max<int64_t>(0, json_int_get(json_object_get(pRoot, "position")));
					State.m_DurationMs = std::max<int64_t>(0, json_int_get(json_object_get(pRoot, "duration")));
					State.m_PositionUpdatedTick = time_get_impl();
					const std::string Track = std::string(State.m_aSourceAppId) + "|" + State.m_aTitle + "|" + State.m_aArtist + "|" + State.m_aAlbum;
					const std::string Encoded = JsonString(pRoot, "cover");
					const bool CoverChanged = Track != PreviousTrack || Encoded != PreviousEncoded;
					auto pCover = CoverChanged ? DecodeCover(pGraphics, Encoded) : nullptr;
					{
						std::lock_guard Lock(m_Mutex);
						m_PendingState = State;
						if(CoverChanged)
						{
							// 不同封面拥有不同代次；解码失败也清除上一首，避免封面串歌。
							m_PendingCoverKey = Track + "|" + std::to_string(State.m_PositionUpdatedTick);
							m_pPendingCover = std::move(pCover);
						}
					}
					PreviousTrack = Track;
					PreviousEncoded = Encoded;
				}
				if(pRoot) json_value_free(pRoot);
			}
			std::unique_lock Lock(m_Mutex);
			m_Wake.wait_for(Lock, std::chrono::milliseconds(50), [&] { return m_Stop || !m_Commands.empty(); });
		}
	}
	void Queue(const char *pCommand)
	{
		std::lock_guard Lock(m_Mutex);
		if(m_Commands.size() < 16) m_Commands.emplace_back(pCommand);
		m_Wake.notify_all();
	}
	void UpdateLyrics(IHttp *pHttp, bool Enabled)
	{
		const std::string Key = Enabled && m_State.m_aTitle[0] ? std::string(m_State.m_aTitle) + "|" + m_State.m_aArtist + "|" + m_State.m_aAlbum : "";
		if(Key != m_LyricsKey)
		{
			if(m_pLyricsRequest) m_pLyricsRequest->Abort();
			m_pLyricsRequest.reset();
			m_Lyrics.Clear();
			m_LyricsKey = Key;
			m_LyricsRetryTick = 0;
		}
		if(Key.empty()) return;
		if(m_pLyricsRequest && m_pLyricsRequest->Done())
		{
			if(m_pLyricsRequest->State() == EHttpState::DONE && m_pLyricsRequest->StatusCode() == 200)
			{
				json_value *pRoot = m_pLyricsRequest->ResultJson();
				if(pRoot) NeteaseLyrics::ParseLrc(JsonString(pRoot, "syncedLyrics"), &m_Lyrics);
				if(pRoot) json_value_free(pRoot);
			}
			m_LyricsRetryTick = time_get() + (m_pLyricsRequest->StatusCode() == 404 ? 600 : 30) * time_freq();
			m_pLyricsRequest.reset();
		}
		if(!pHttp || m_pLyricsRequest || m_Lyrics.m_HasTiming || time_get() < m_LyricsRetryTick) return;
		char aTitle[512], aArtist[512], aAlbum[512], aUrl[2048];
		EscapeUrl(aTitle, m_State.m_aTitle);
		EscapeUrl(aArtist, m_State.m_aArtist);
		EscapeUrl(aAlbum, m_State.m_aAlbum);
		str_format(aUrl, sizeof(aUrl), "https://lrclib.net/api/get?track_name=%s&artist_name=%s&album_name=%s", aTitle, aArtist, aAlbum);
		m_pLyricsRequest = HttpGet(aUrl);
		m_pLyricsRequest->Timeout(CTimeout{10000, 0, 500, 10});
		m_pLyricsRequest->FailOnErrorStatus(false);
		m_pLyricsRequest->LogProgress(HTTPLOG::FAILURE);
		pHttp->Run(m_pLyricsRequest);
	}
};

CSystemMediaControls::CSystemMediaControls() = default;
CSystemMediaControls::~CSystemMediaControls() = default;

void CSystemMediaControls::OnInit() {}

void CSystemMediaControls::OnShutdown()
{
	if(!m_pImpl) return;
	// 先结束采集再销毁纹理，保证后台不会在图形接口销毁后继续解码。
	m_pImpl->m_Stop = true;
	m_pImpl->m_Wake.notify_all();
	if(m_pImpl->m_Thread.joinable()) m_pImpl->m_Thread.join();
	GameClient()->m_Hud.ReleaseMediaIslandTracks();
	Graphics()->UnloadTexture(&m_pImpl->m_State.m_AlbumArt);
	Graphics()->UnloadTexture(&m_pImpl->m_State.m_AlbumArtCircular);
	for(auto &Cover : m_pImpl->m_vRetiredCovers) Graphics()->UnloadTexture(&Cover.m_Texture);
	m_pImpl.reset();
}

void CSystemMediaControls::OnUpdate()
{
	if(!g_Config.m_QmSmtcEnable)
	{
		OnShutdown();
		return;
	}
	if(!m_pImpl)
	{
		char aScript[IO_MAX_PATH_LENGTH] = "";
		for(int i = 0; i < Storage()->NumPaths(); ++i)
		{
			if(!Storage()->FileExists("tclient/qm_system_media.ps1", i)) continue;
			Storage()->GetCompletePath(i, "tclient/qm_system_media.ps1", aScript, sizeof(aScript));
			break;
		}
		m_pImpl = std::make_unique<CImpl>();
		m_pImpl->m_Thread = std::thread(&CImpl::Run, m_pImpl.get(), std::string(aScript), Graphics());
	}
	auto &Impl = *m_pImpl;
	std::unique_ptr<SPreparedCover> pCover;
	bool CoverChanged = false;
	{
		std::lock_guard Lock(Impl.m_Mutex);
		const auto Original = Impl.m_State.m_AlbumArt;
		const auto Circular = Impl.m_State.m_AlbumArtCircular;
		const int Width = Impl.m_State.m_AlbumArtWidth, Height = Impl.m_State.m_AlbumArtHeight;
		Impl.m_State = Impl.m_PendingState;
		Impl.m_State.m_AlbumArt = Original;
		Impl.m_State.m_AlbumArtCircular = Circular;
		Impl.m_State.m_AlbumArtWidth = Width;
		Impl.m_State.m_AlbumArtHeight = Height;
		CoverChanged = Impl.m_AppliedCoverKey != Impl.m_PendingCoverKey;
		if(CoverChanged)
		{
			Impl.m_AppliedCoverKey = Impl.m_PendingCoverKey;
			pCover = std::move(Impl.m_pPendingCover);
		}
	}
	if(CoverChanged)
	{
		// Qm 的切歌动画会继续引用旧封面；过渡结束前保留纹理。
		for(auto Texture : {Impl.m_State.m_AlbumArt, Impl.m_State.m_AlbumArtCircular})
			if(Texture.IsValid()) Impl.m_vRetiredCovers.push_back({Texture, time_get() + 2 * time_freq()});
		Impl.m_State.m_AlbumArt.Invalidate();
		Impl.m_State.m_AlbumArtCircular.Invalidate();
		Impl.m_State.m_AlbumArtWidth = Impl.m_State.m_AlbumArtHeight = 0;
		if(pCover)
		{
			Impl.m_State.m_AlbumArtWidth = pCover->m_Original.m_Width;
			Impl.m_State.m_AlbumArtHeight = pCover->m_Original.m_Height;
			Impl.m_State.m_AlbumArt = Graphics()->LoadTextureRawMove(pCover->m_Original, 0, "qm_album_art");
			Impl.m_State.m_AlbumArtCircular = Graphics()->LoadTextureRawMove(pCover->m_Circular, 0, "qm_album_art_circle");
		}
	}
	for(auto It = Impl.m_vRetiredCovers.begin(); It != Impl.m_vRetiredCovers.end();)
	{
		if(time_get() >= It->m_ReleaseTick && !GameClient()->m_Hud.ReferencesMediaIslandTexture(It->m_Texture))
		{
			Graphics()->UnloadTexture(&It->m_Texture);
			It = Impl.m_vRetiredCovers.erase(It);
		}
		else ++It;
	}
	SState Current;
	Impl.UpdateLyrics(Http(), g_Config.m_QmLyricsInMediaIsland && GetStateSnapshot(Current));
}

bool CSystemMediaControls::GetStateSnapshot(SState &State) const
{
	if(!m_pImpl || !g_Config.m_QmSmtcEnable) return false;
	State = m_pImpl->m_State;
	if(State.m_PositionUpdatedTick <= 0 || time_get() - State.m_PositionUpdatedTick > 5 * time_freq()) return false;
	if(!State.m_aTitle[0] && !State.m_aArtist[0] && !State.m_aAlbum[0]) return false;
	if(State.m_Playing) State.m_PositionMs += std::max<int64_t>(0, time_get() - State.m_PositionUpdatedTick) * 1000 / time_freq();
	if(State.m_DurationMs > 0) State.m_PositionMs = std::min(State.m_PositionMs, State.m_DurationMs);
	return true;
}

bool CSystemMediaControls::HasActiveLyrics() const
{
	SState State;
	return m_pImpl && g_Config.m_QmLyricsInMediaIsland && GetStateSnapshot(State) && m_pImpl->m_Lyrics.m_HasTiming;
}

bool CSystemMediaControls::GetCurrentLyric(char *pText, size_t Size) const
{
	if(Size) pText[0] = '\0';
	SState State;
	if(!HasActiveLyrics() || !GetStateSnapshot(State)) return false;
	const auto Line = NeteaseLyrics::SelectLatestStartedLine(m_pImpl->m_Lyrics, State.m_PositionMs);
	if(!Line.m_pLine) return false;
	str_copy(pText, Line.m_pLine->m_Text.c_str(), Size);
	return pText[0] != '\0';
}

void CSystemMediaControls::Previous() { if(m_pImpl) m_pImpl->Queue("previous\n"); }
void CSystemMediaControls::PlayPause() { if(m_pImpl) m_pImpl->Queue("playpause\n"); }
void CSystemMediaControls::Next() { if(m_pImpl) m_pImpl->Queue("next\n"); }

void CSystemMediaControls::SetVolume(uint64_t Generation, float Volume)
{
	// Windows SMTC 不提供会话音量写入；能力标记为 false，Qm 原版控件显示不可用。
	(void)Generation;
	(void)Volume;
}
