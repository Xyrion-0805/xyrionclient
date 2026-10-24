// Xyrion platform adapter for BestClient's music player on MinGW.
// Uses the same Windows SMTC API through the bundled hidden WinRT helper.
#if defined(CONF_FAMILY_WINDOWS) && !BC_MUSICPLAYER_HAS_WINRT
class CWindowsBridgeNowPlayingProvider final : public IMusicPlaybackProvider
{
	HANDLE m_Process = nullptr, m_Output = nullptr, m_Input = nullptr;
	std::string m_Buffer;
	SNowPlayingSnapshot m_Last;
	int64_t m_Received = 0;

	void Control(const char *pAction)
	{
		if(!m_Input) return;
		DWORD Written;
		WriteFile(m_Input, pAction, str_length(pAction), &Written, nullptr);
	}

public:
	explicit CWindowsBridgeNowPlayingProvider(IStorage *pStorage)
	{
		char aScript[IO_MAX_PATH_LENGTH];
		bool Found = false;
		for(int i = 0; i < pStorage->NumPaths(); ++i)
		{
			if(!pStorage->FileExists("tclient/bestclient_media.ps1", i)) continue;
			pStorage->GetCompletePath(i, "tclient/bestclient_media.ps1", aScript, sizeof(aScript));
			Found = true;
			break;
		}
		if(!Found) return;
		SECURITY_ATTRIBUTES Security{sizeof(Security), nullptr, TRUE};
		HANDLE ChildOutput = nullptr, ChildInput = nullptr;
		if(!CreatePipe(&m_Output, &ChildOutput, &Security, 0)) return;
		if(!CreatePipe(&ChildInput, &m_Input, &Security, 0)) { CloseHandle(ChildOutput); return; }
		SetHandleInformation(m_Output, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(m_Input, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW Startup{};
		Startup.cb = sizeof(Startup);
		Startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
		Startup.wShowWindow = SW_HIDE;
		Startup.hStdOutput = Startup.hStdError = ChildOutput;
		Startup.hStdInput = ChildInput;
		PROCESS_INFORMATION Process{};
		wchar_t aWindows[MAX_PATH];
		GetSystemDirectoryW(aWindows, MAX_PATH);
		std::wstring Application = std::wstring(aWindows) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
		const std::wstring Script = windows_utf8_to_wide(aScript);
		std::wstring Command = L"\"" + Application + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + Script + L"\" -ParentId " + std::to_wstring(GetCurrentProcessId());
		if(CreateProcessW(Application.c_str(), Command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &Startup, &Process))
		{
			m_Process = Process.hProcess;
			CloseHandle(Process.hThread);
		}
		CloseHandle(ChildInput);
		CloseHandle(ChildOutput);
	}

	~CWindowsBridgeNowPlayingProvider() override
	{
		if(m_Input) CloseHandle(m_Input);
		if(m_Output) CloseHandle(m_Output);
		if(m_Process)
		{
			if(WaitForSingleObject(m_Process, 0) == WAIT_TIMEOUT) TerminateProcess(m_Process, 0);
			CloseHandle(m_Process);
		}
	}

	bool Poll(SNowPlayingSnapshot &Out) override
	{
		if(!m_Process || WaitForSingleObject(m_Process, 0) != WAIT_TIMEOUT) return false;
		DWORD Available = 0, Read = 0;
		char aRead[16384];
		for(int i = 0; i < 128 && PeekNamedPipe(m_Output, nullptr, 0, nullptr, &Available, nullptr) && Available; ++i)
		{
			if(!ReadFile(m_Output, aRead, std::min<DWORD>(Available, sizeof(aRead)), &Read, nullptr)) break;
			m_Buffer.append(aRead, Read);
			if(m_Buffer.size() > 12 * 1024 * 1024) { m_Buffer.clear(); break; }
		}
		size_t End;
		while((End = m_Buffer.find('\n')) != std::string::npos)
		{
			json_value *pJson = JsonParse(m_Buffer.data(), End);
			if(pJson && pJson->type == json_object)
			{
				auto String = [&](const char *pKey) { return std::string(json_string_get(json_object_get(pJson, pKey))); };
				SNowPlayingSnapshot Snapshot;
				Snapshot.m_Title = String("title");
				Snapshot.m_Artist = String("artist");
				Snapshot.m_Album = String("album");
				Snapshot.m_ServiceId = String("app");
				Snapshot.m_Valid = !Snapshot.m_Title.empty();
				Snapshot.m_PositionMs = std::max<int64_t>(0, (int64_t)json_int_get(json_object_get(pJson, "position")));
				Snapshot.m_DurationMs = std::max<int64_t>(0, (int64_t)json_int_get(json_object_get(pJson, "duration")));
				Snapshot.m_PlaybackState = json_boolean_get(json_object_get(pJson, "playing")) ? EMusicPlaybackState::PLAYING : EMusicPlaybackState::PAUSED;
				Snapshot.m_CanPrev = json_boolean_get(json_object_get(pJson, "previous"));
				Snapshot.m_CanNext = json_boolean_get(json_object_get(pJson, "next"));
				Snapshot.m_CanPlayPause = json_boolean_get(json_object_get(pJson, "playpause"));
				const std::string Cover = String("cover");
				Snapshot.m_Art.m_Key = Snapshot.m_ServiceId + "|" + Snapshot.m_Title + "|" + Snapshot.m_Artist + "|" + Snapshot.m_Album;
				if(!Cover.empty() && Cover.size() <= 12 * 1024 * 1024)
				{
					Snapshot.m_Art.m_vBytes.resize(Cover.size() * 3 / 4 + 3);
					const int Size = str_base64_decode(Snapshot.m_Art.m_vBytes.data(), Snapshot.m_Art.m_vBytes.size(), Cover.c_str());
					if(Size > 0)
					{
						Snapshot.m_Art.m_vBytes.resize(Size);
						Snapshot.m_Art.m_Type = SMusicArt::EType::BYTES;
						Snapshot.m_Art.m_Key += "|" + std::to_string(HashBytes(Cover));
					}
				}
				m_Last = std::move(Snapshot);
				m_Received = time_get();
			}
			if(pJson) json_value_free(pJson);
			m_Buffer.erase(0, End + 1);
		}
		if(!m_Received || time_get() - m_Received > 5 * time_freq()) return false;
		Out = m_Last;
		if(Out.m_PlaybackState == EMusicPlaybackState::PLAYING)
			Out.m_PositionMs += (time_get() - m_Received) * 1000 / time_freq();
		return Out.m_Valid;
	}

	void Previous() override { Control("previous\n"); }
	void PlayPause() override { Control("playpause\n"); }
	void Next() override { Control("next\n"); }
};
#endif
