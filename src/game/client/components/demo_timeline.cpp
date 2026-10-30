#include "demo_timeline.h"

#include <base/io.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/client.h>
#include <engine/client/demo_project_export.h>
#include <engine/demo.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

namespace
{
std::string TimeLabel(double Seconds)
{
	const int64_t Hundredths = std::max<int64_t>(0, (int64_t)std::round(Seconds * 100));
	char aText[40];
	str_format(aText, sizeof(aText), "%02lld:%02lld.%02lld", (long long)(Hundredths / 6000), (long long)(Hundredths / 100 % 60), (long long)(Hundredths % 100));
	return aText;
}
}

bool CDemoTimeline::Busy() const { return m_Rendering || m_PendingVideo || m_pExport != nullptr; }

void CDemoTimeline::Message(const std::string &Text)
{
	m_Status = Text;
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_editor", Text.c_str());
}

bool CDemoTimeline::EnsureSource()
{
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK || !DemoPlayer()->IsPlaying()) return false;
	const auto *pInfo = DemoPlayer()->BaseInfo();
	if(pInfo->m_LiveDemo || pInfo->m_LastTick <= pInfo->m_FirstTick)
	{
		Message("请等待录制结束后再剪辑此 Demo");
		return false;
	}
	if(m_Project.Source() != DemoPlayer()->SourceFilename())
	{
		if(Busy()) return false;
		m_Project.Reset(DemoPlayer()->SourceFilename(), pInfo->m_FirstTick, pInfo->m_LastTick, 50);
		m_In = pInfo->m_FirstTick;
		m_Out = pInfo->m_LastTick;
		m_Selected = -1;
		m_Cursor = 0;
		char aStamp[32], aName[64];
		str_timestamp(aStamp, sizeof(aStamp));
		str_format(aName, sizeof(aName), "cut-%s", aStamp);
		m_Name.Set(aName);
		Select(-1);
	}
	return true;
}

void CDemoTimeline::Toggle()
{
	if(Busy()) return;
	if(m_Open)
	{
		StopPreview();
		m_Open = false;
		return;
	}
	if(!EnsureSource()) return;
	m_Open = true;
	DemoPlayer()->Pause();
	GameClient()->m_Menus.SetActive(true);
}

void CDemoTimeline::Select(int Index)
{
	m_Selected = Index >= 0 && Index < (int)m_Project.Clips().size() ? Index : -1;
	if(m_Selected >= 0)
	{
		m_In = m_Project.Clips()[m_Selected].m_In;
		m_Out = m_Project.Clips()[m_Selected].m_Out;
		m_Cursor = m_Project.ClipStart(m_Selected);
	}
	m_InInput.SetInteger(m_In - m_Project.FirstTick());
	m_OutInput.SetInteger(m_Out - m_Project.FirstTick());
}

void CDemoTimeline::Changed()
{
	Select(std::min(m_Selected, (int)m_Project.Clips().size() - 1));
	m_Cursor = std::clamp(m_Cursor, 0.0, m_Project.Duration());
}

void CDemoTimeline::Seek(int Tick)
{
	GameClient()->m_Chat.Reset();
	GameClient()->m_DamageInd.OnReset();
	GameClient()->m_InfoMessages.OnReset();
	GameClient()->m_Particles.OnReset();
	GameClient()->m_Sounds.OnReset();
	const bool OldSuppress = GameClient()->m_SuppressEvents;
	GameClient()->m_SuppressEvents = true;
	DemoPlayer()->SetPos(std::clamp(Tick + 1, m_Project.FirstTick() + 1, m_Project.LastTick()));
	GameClient()->m_SuppressEvents = OldSuppress;
}

void CDemoTimeline::StopPreview()
{
	if(!m_Playing) return;
	m_Playing = false;
	DemoPlayer()->SetTimelineTime(-1);
	DemoPlayer()->Pause();
	DemoPlayer()->SetSpeed(m_OldSpeed);
	m_PlayClip = -1;
}

void CDemoTimeline::Preview()
{
	if(m_Playing) { StopPreview(); return; }
	if(Busy() || m_Project.Clips().empty()) return;
	m_OldSpeed = DemoPlayer()->BaseInfo()->m_Speed;
	m_PlayOffset = m_Cursor >= m_Project.Duration() ? 0 : m_Cursor;
	m_PlayStart = time_get();
	m_PlayClip = -1;
	m_Playing = true;
	DemoPlayer()->Unpause();
}

bool CDemoTimeline::ValidName() const
{
	const char *pName = m_Name.GetString();
	if(!pName[0] || str_length(pName) > 60) return false;
	for(; *pName; ++pName)
		if(!((*pName >= 'a' && *pName <= 'z') || (*pName >= 'A' && *pName <= 'Z') || (*pName >= '0' && *pName <= '9') || *pName == '-' || *pName == '_')) return false;
	return true;
}

void CDemoTimeline::Save()
{
	if(!ValidName()) { Message("工程名请使用字母、数字、短横线或下划线（最多 60 字符）"); return; }
	Storage()->CreateFolder("demo-projects", IStorage::TYPE_SAVE);
	const std::string Path = "demo-projects/" + std::string(m_Name.GetString()) + ".xyrion-cut.json";
	const std::string Temp = Path + ".tmp";
	const std::string Json = m_Project.Serialize();
	IOHANDLE File = Storage()->OpenFile(Temp.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File) { Message("无法保存工程"); return; }
	const bool Written = io_write(File, Json.data(), Json.size()) == Json.size();
	const bool Closed = io_close(File) == 0;
	if(!Written || !Closed || !Storage()->RenameFile(Temp.c_str(), Path.c_str(), IStorage::TYPE_SAVE))
	{
		Message("保存失败，临时工程保留在 " + Temp);
		return;
	}
	Message("工程已保存：" + Path);
}

void CDemoTimeline::Load()
{
	if(!ValidName()) { Message("请输入有效的工程名称"); return; }
	const std::string Path = "demo-projects/" + std::string(m_Name.GetString()) + ".xyrion-cut.json";
	IOHANDLE File = Storage()->OpenFile(Path.c_str(), IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File) { Message("未找到工程：" + Path); return; }
	const int64_t Length = io_length(File);
	std::string Data;
	bool Read = Length > 0 && Length <= 1024 * 1024;
	if(Read)
	{
		Data.resize((size_t)Length);
		Read = io_read(File, Data.data(), Data.size()) == Data.size();
	}
	io_close(File);
	demo_edit::CProject Candidate;
	std::string Error;
	if(!Read || !Candidate.Load(Data, Error)) { Message("工程损坏或格式不受支持：" + Error); return; }
	if(Candidate.Source() != m_Project.Source() || Candidate.FirstTick() != m_Project.FirstTick() || Candidate.LastTick() != m_Project.LastTick())
	{
		Message("请先打开工程对应的源 Demo：" + Candidate.Source());
		return;
	}
	StopPreview();
	m_Project = std::move(Candidate);
	Select(0);
	Message("工程已读取");
}

void CDemoTimeline::Export(bool Video)
{
	if(Busy() || m_Project.Clips().empty()) { Message("请先添加片段，并等待当前导出完成"); return; }
	if(!ValidName()) { Message("导出名称请使用字母、数字、短横线或下划线"); return; }
	StopPreview();
	Save();
	if(!Video)
	{
		m_pExport = Client()->ExportDemoClips(m_Project.Clips(), ("demos/edits/" + std::string(m_Name.GetString())).c_str(), m_RemoveChat);
		Message(m_pExport ? "正在后台导出 Demo 片段…" : "无法开始导出");
		return;
	}
	// Start on update, after the UI frame that requested export has finished.
	m_PendingVideo = true;
}

void CDemoTimeline::StartVideo()
{
	m_PendingVideo = false;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current()) { Message("请先停止当前视频录制"); return; }
	m_VideoFinal = "videos/" + std::string(m_Name.GetString()) + ".mp4";
	m_VideoTemp = "videos/" + std::string(m_Name.GetString()) + "_rendering.mp4";
	if(Storage()->FileExists(m_VideoFinal.c_str(), IStorage::TYPE_SAVE) || Storage()->FileExists(m_VideoTemp.c_str(), IStorage::TYPE_SAVE))
	{
		Message("视频名称已存在，请修改工程名后导出"); return;
	}
	m_OldSpeed = DemoPlayer()->BaseInfo()->m_Speed;
	m_OldPauseWithDemo = g_Config.m_ClVideoPauseWithDemo;
	g_Config.m_ClVideoPauseWithDemo = 0;
	DemoPlayer()->Pause();
	DemoPlayer()->SetTimelineTime(m_Project.Clips()[0].m_In / 50.0);
	Seek(m_Project.Clips()[0].m_In);
	// SafeName is strictly validated above; no console metacharacters are possible.
	Console()->ExecuteLine(("start_video " + std::string(m_Name.GetString()) + "_rendering").c_str(), -1);
	if(!IVideo::Current())
	{
		DemoPlayer()->SetTimelineTime(-1);
		g_Config.m_ClVideoPauseWithDemo = m_OldPauseWithDemo;
		Message("无法启动 MP4 编码，请查看本地控制台"); return;
	}
	m_Rendering = true;
	m_VideoStart = IVideo::Current()->Time();
	m_Cursor = 0;
	m_PlayClip = -1;
	GameClient()->m_Menus.SetActive(false);
	if(CLineInput::GetActiveInput()) CLineInput::GetActiveInput()->Deactivate();
	IVideo::Current()->Pause(false);
	DemoPlayer()->Unpause();
	Message("正在导出 MP4，按 Esc 取消；导出界面不会录入视频");
#else
	Message("当前构建没有启用视频编码器");
#endif
}

void CDemoTimeline::FinishVideo(bool Success)
{
	if(!m_Rendering) return;
	m_Rendering = false;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current()) IVideo::Current()->Stop();
	g_Config.m_ClVideoPauseWithDemo = m_OldPauseWithDemo;
#endif
	DemoPlayer()->SetTimelineTime(-1);
	if(DemoPlayer()->IsPlaying())
	{
		DemoPlayer()->Pause();
		DemoPlayer()->SetSpeed(m_OldSpeed);
	}
	GameClient()->m_Menus.SetActive(true);
	m_PlayClip = -1;
	if(Success && Storage()->RenameFile(m_VideoTemp.c_str(), m_VideoFinal.c_str(), IStorage::TYPE_SAVE))
		Message("MP4 导出完成：" + m_VideoFinal);
	else
	{
		// Keep playable partial output on cancellation or failure.
		Message("导出取消或未完成，已保留视频：" + m_VideoTemp);
	}
}

void CDemoTimeline::OnUpdate()
{
	if(m_PendingVideo) StartVideo();
	if(m_pExport && m_pExport->State() == IJob::STATE_DONE)
	{
		Message(m_pExport->Success() ? "Demo 导出完成：" + m_pExport->Folder() : m_pExport->Error());
		m_pExport.reset();
	}
	if(!m_Open || (!m_Playing && !m_Rendering)) return;
	if(!DemoPlayer()->IsPlaying()) { FinishVideo(false); StopPreview(); return; }
	double Seconds = 0;
#if defined(CONF_VIDEORECORDER)
	if(m_Rendering)
	{
		if(!IVideo::Current()) { FinishVideo(false); return; }
		// The recorder discards its first presented frame while the GPU pipeline
		// warms up. Hold the source for that frame so no content is lost.
		Seconds = std::max(0.0, (IVideo::Current()->Time() - m_VideoStart) / (double)time_freq() - 1.0 / g_Config.m_ClVideoRecorderFPS);
	}
	else
#endif
		Seconds = m_PlayOffset + (time_get() - m_PlayStart) / (double)time_freq();
	m_Cursor = std::min(Seconds, m_Project.Duration());
	if(Seconds + 0.000001 >= m_Project.Duration())
	{
		if(m_Rendering) FinishVideo(true); else StopPreview();
		return;
	}
	const auto Position = m_Project.Position(Seconds);
	if(Position.m_Clip < 0) return;
	const auto &Clip = m_Project.Clips()[Position.m_Clip];
	if(m_PlayClip != Position.m_Clip)
	{
		DemoPlayer()->SetTimelineTime(Position.m_SourceTick / 50.0);
		Seek(Position.m_SourceTick);
		m_PlayClip = Position.m_Clip;
		DemoPlayer()->SetSpeed(Clip.m_Speed / 100.0f);
		DemoPlayer()->Unpause();
	}
	const double SourceTime = Clip.m_In / 50.0 + (Seconds - m_Project.ClipStart(Position.m_Clip)) * Clip.m_Speed / 100.0;
	DemoPlayer()->SetTimelineTime(SourceTime);
}

void CDemoTimeline::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_DEMOPLAYBACK) return;
	m_PendingVideo = false;
	FinishVideo(false);
	StopPreview();
	m_Open = false;
}

void CDemoTimeline::OnShutdown()
{
	m_PendingVideo = false;
	FinishVideo(false);
	StopPreview();
	if(m_pExport) m_pExport->Cancel();
}

bool CDemoTimeline::OnInput(const IInput::CEvent &Event)
{
	if(!m_Open) return false;
	if(m_Rendering)
	{
		if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE) FinishVideo(false);
		return true;
	}
	if(GameClient()->m_GameConsole.IsActive() || CLineInput::GetActiveInput() || !(Event.m_Flags & IInput::FLAG_PRESS)) return false;
	if(Event.m_Key == KEY_ESCAPE)
	{
		if(m_pExport) m_pExport->Cancel();
		else if(m_Playing) StopPreview();
		else m_Open = false;
		return true;
	}
	if(Busy()) return false;
	const bool Ctrl = Input()->ModifierIsPressed();
	if(Ctrl && Event.m_Key == KEY_S) { Save(); return true; }
	if(Ctrl && Event.m_Key == KEY_Z) { StopPreview(); if(Input()->ShiftIsPressed()) m_Project.Redo(); else m_Project.Undo(); Changed(); return true; }
	if(Ctrl && Event.m_Key == KEY_Y) { StopPreview(); m_Project.Redo(); Changed(); return true; }
	if(Ctrl) return false;
	if(Event.m_Key == KEY_SPACE) { Preview(); return true; }
	if(m_Playing) return false;
	const int Tick = DemoPlayer()->BaseInfo()->m_CurrentTick;
	if(Event.m_Key == KEY_I) { m_In = Tick; m_InInput.SetInteger(Tick - m_Project.FirstTick()); return true; }
	if(Event.m_Key == KEY_O) { m_Out = Tick; m_OutInput.SetInteger(Tick - m_Project.FirstTick()); return true; }
	if(Event.m_Key == KEY_S) { if(m_Project.Split(m_Selected, Tick)) Changed(); return true; }
	if(Event.m_Key == KEY_DELETE) { m_Project.Remove(m_Selected); Changed(); return true; }
	if(Event.m_Key == KEY_COMMA || Event.m_Key == KEY_PERIOD)
	{
		Seek(Tick + (Event.m_Key == KEY_COMMA ? -1 : 1));
		DemoPlayer()->Pause(); return true;
	}
	return false;
}

void CDemoTimeline::OnConsoleInit()
{
	Console()->Register("toggle_demo_editor", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { static_cast<CDemoTimeline *>(pUser)->Toggle(); }, this, "Open the demo timeline editor");
	Console()->Register("demo_editor", "s[action] ?i[value] ?i[value]", CFGFLAG_CLIENT, ConAction, this, "Actions: add/trim/split/select/move/speed/undo/redo/preview/save/load/demo/mp4/status/cancel. Ticks are relative to source start.");
	Console()->Register("demo_editor_name", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) { static_cast<CDemoTimeline *>(pUser)->m_Name.Set(pResult->GetString(0)); }, this, "Project/export name (letters, numbers, dash, underscore)");
}

void CDemoTimeline::ConAction(IConsole::IResult *pResult, void *pUser)
{
	auto *pSelf = static_cast<CDemoTimeline *>(pUser);
	if(!pSelf->EnsureSource()) return;
	const std::string Action = pResult->GetString(0);
	if(Action == "cancel")
	{
		pSelf->m_PendingVideo = false;
		pSelf->FinishVideo(false);
		if(pSelf->m_pExport) pSelf->m_pExport->Cancel();
		return;
	}
	if(Action == "status")
	{
		char aText[256];
		str_format(aText, sizeof(aText), "clips=%d duration=%.3f selected=%d playing=%d rendering=%d export=%d; %s", (int)pSelf->m_Project.Clips().size(), pSelf->m_Project.Duration(), pSelf->m_Selected, pSelf->m_Playing, pSelf->m_Rendering, pSelf->m_pExport ? pSelf->m_pExport->Completed() : -1, pSelf->m_Status.c_str());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_editor", aText);
		return;
	}
	if(pSelf->Busy()) return;
	if(Action == "preview") { if(!pSelf->m_Open) pSelf->Toggle(); pSelf->Preview(); return; }
	pSelf->StopPreview();
	const int A = pResult->NumArguments() > 1 ? std::clamp(pResult->GetInteger(1), -1000000000, 1000000000) : 0;
	const int B = pResult->NumArguments() > 2 ? std::clamp(pResult->GetInteger(2), -1000000000, 1000000000) : 0;
	const int First = pSelf->m_Project.FirstTick();
	bool Changed = false;
	if(Action == "add" && pResult->NumArguments() == 3) { Changed = pSelf->m_Project.Add(First + A, First + B); if(Changed) pSelf->m_Selected = (int)pSelf->m_Project.Clips().size() - 1; }
	else if(Action == "trim" && pResult->NumArguments() == 3) Changed = pSelf->m_Project.Trim(pSelf->m_Selected, First + A, First + B);
	else if(Action == "split" && pResult->NumArguments() == 2) Changed = pSelf->m_Project.Split(pSelf->m_Selected, First + A);
	else if(Action == "select") pSelf->Select(A);
	else if(Action == "move") { Changed = pSelf->m_Project.Move(pSelf->m_Selected, A); if(Changed) pSelf->m_Selected = A; }
	else if(Action == "speed") Changed = pSelf->m_Project.SetSpeed(pSelf->m_Selected, A);
	else if(Action == "remove") Changed = pSelf->m_Project.Remove(pSelf->m_Selected);
	else if(Action == "undo") Changed = pSelf->m_Project.Undo();
	else if(Action == "redo") Changed = pSelf->m_Project.Redo();
	else if(Action == "save") pSelf->Save();
	else if(Action == "load") pSelf->Load();
	else if(Action == "demo" || Action == "mp4") { if(!pSelf->m_Open) pSelf->Toggle(); pSelf->Export(Action == "mp4"); }
	if(Changed) pSelf->Changed();
}

bool CDemoTimeline::Button(int Id, const char *pText, CUIRect &Row, float Width, bool Enabled)
{
	CUIRect Rect;
	Row.VSplitLeft(Width, &Rect, &Row);
	Row.VSplitLeft(4, nullptr, &Row);
	return ui_widget::SecondaryButton(ui_widget::ControlContext(Ui()), &m_aButtons[Id], pText, Rect, !Enabled);
}

void CDemoTimeline::Render(CUIRect Screen)
{
	if(m_Rendering || !m_Open) return;
	if(!EnsureSource()) return;
	GameClient()->m_Menus.SetActive(true);
	const auto OldColor = TextRender()->GetTextColor();
	TextRender()->TextColor(ColorRGBA(0.92f, 0.93f, 0.95f, 1));
	const bool Editable = !Busy() && !m_Playing;
	const bool Selected = m_Selected >= 0 && m_Selected < (int)m_Project.Clips().size();
	CUIRect Header = Screen, Title, Row, Panel;
	Header.HSplitTop(36, &Header, nullptr);
	Header.Draw(ColorRGBA(0.075f, 0.08f, 0.095f, 0.97f), 0, 0);
	Header.Margin(6, &Header);
	Header.VSplitLeft(std::max(145.0f, Header.w - 660), &Title, &Header);
	Ui()->DoLabel(&Title, "DEMO / 剪辑工作台", 15, TEXTALIGN_ML);
	CUIRect Name;
	Header.VSplitLeft(170, &Name, &Header);
	Ui()->DoEditBox(&m_Name, &Name, 12);
	Header.VSplitLeft(8, nullptr, &Header);
	if(Button(0, "保存", Header, 46, Editable)) Save();
	if(Button(1, "读取", Header, 46, Editable)) Load();
	if(Button(2, "导出 Demo", Header, 80, Editable)) Export(false);
	if(Button(3, "导出 MP4", Header, 80, Editable)) Export(true);
	if(Button(4, Busy() ? "取消导出" : "返回播放", Header, 76))
	{
		if(m_pExport) m_pExport->Cancel(); else Toggle();
	}
	Screen.HSplitBottom(285, nullptr, &Panel);
	Panel.Draw(ColorRGBA(0.065f, 0.07f, 0.085f, 0.97f), IGraphics::CORNER_T, 10);
	Panel.Margin(10, &Panel);
	Panel.HSplitTop(24, &Row, &Panel);
	if(Button(5, m_Playing ? "暂停" : "预览成片", Row, 76, !Busy())) Preview();
	if(Button(6, "撤销", Row, 48, Editable && m_Project.CanUndo())) { m_Project.Undo(); Changed(); }
	if(Button(7, "重做", Row, 48, Editable && m_Project.CanRedo())) { m_Project.Redo(); Changed(); }
	if(Button(8, "分割 S", Row, 58, Editable && Selected)) { m_Project.Split(m_Selected, DemoPlayer()->BaseInfo()->m_CurrentTick); Changed(); }
	if(Button(9, "复制", Row, 48, Editable && Selected)) { m_Project.Duplicate(m_Selected); Changed(); }
	if(Button(10, "删除", Row, 48, Editable && Selected)) { m_Project.Remove(m_Selected); Changed(); }
	if(Button(11, "前移", Row, 48, Editable && m_Selected > 0)) { m_Project.Move(m_Selected, m_Selected - 1); --m_Selected; Changed(); }
	if(Button(12, "后移", Row, 48, Editable && Selected && m_Selected + 1 < (int)m_Project.Clips().size())) { m_Project.Move(m_Selected, m_Selected + 1); ++m_Selected; Changed(); }
	if(Button(13, "缩小", Row, 48)) m_Zoom = std::max(1.0f, m_Zoom / 1.5f);
	if(Button(14, "放大", Row, 48)) m_Zoom = std::min(16.0f, m_Zoom * 1.5f);
	if(Button(15, "适应", Row, 48)) { m_Zoom = 1; m_Scroll = 0; }
	Ui()->DoLabel(&Row, (TimeLabel(m_Cursor) + " / " + TimeLabel(m_Project.Duration())).c_str(), 12, TEXTALIGN_MR);
	Panel.HSplitTop(8, nullptr, &Panel);
	Panel.HSplitTop(20, &Row, &Panel);
	char aSourceName[IO_MAX_PATH_LENGTH];
	DemoPlayer()->GetDemoName(aSourceName, sizeof(aSourceName));
	Ui()->DoLabel(&Row, ("源素材  /  " + std::string(aSourceName) + "   " + TimeLabel((DemoPlayer()->BaseInfo()->m_CurrentTick - m_Project.FirstTick()) / 50.0)).c_str(), 12, TEXTALIGN_ML);
	Panel.HSplitTop(20, &Row, &Panel);
	const int Total = m_Project.LastTick() - m_Project.FirstTick();
	const float Fraction = (DemoPlayer()->BaseInfo()->m_CurrentTick - m_Project.FirstTick()) / (float)Total;
	const float NewFraction = Ui()->DoScrollbarH(&m_SourceSlider, &Row, Fraction);
	const int MarkIn = std::clamp(m_InInput.GetInteger(), 0, Total);
	const int MarkOut = std::clamp(m_OutInput.GetInteger(), 0, Total);
	if(MarkOut > MarkIn)
	{
		CUIRect Range = Row;
		Range.x += Row.w * MarkIn / Total;
		Range.w = Row.w * (MarkOut - MarkIn) / Total;
		Range.y += Range.h - 2; Range.h = 2;
		Range.Draw(ColorRGBA(0.85f, 0.68f, 0.43f, 0.8f), 0, 0);
	}
	if(Editable && std::abs(NewFraction - Fraction) > 0.000001f) { Seek(m_Project.FirstTick() + (int)std::round(NewFraction * Total)); DemoPlayer()->Pause(); }
	Panel.HSplitTop(6, nullptr, &Panel);
	Panel.HSplitTop(24, &Row, &Panel);
	if(Button(16, "入点 I", Row, 60, Editable)) { m_In = DemoPlayer()->BaseInfo()->m_CurrentTick; m_InInput.SetInteger(m_In - m_Project.FirstTick()); }
	CUIRect Field;
	Row.VSplitLeft(76, &Field, &Row);
	Ui()->DoEditBox(&m_InInput, &Field, 12);
	Row.VSplitLeft(4, nullptr, &Row);
	if(Button(17, "出点 O", Row, 60, Editable)) { m_Out = DemoPlayer()->BaseInfo()->m_CurrentTick; m_OutInput.SetInteger(m_Out - m_Project.FirstTick()); }
	Row.VSplitLeft(76, &Field, &Row);
	Ui()->DoEditBox(&m_OutInput, &Field, 12);
	Row.VSplitLeft(4, nullptr, &Row);
	if(Button(18, "添加选区", Row, 76, Editable))
	{
		if(m_Project.Add(m_Project.FirstTick() + m_InInput.GetInteger(), m_Project.FirstTick() + m_OutInput.GetInteger())) Select((int)m_Project.Clips().size() - 1);
		else Message("选区必须在源素材范围内，且出点大于入点；最多 64 片段");
	}
	if(Button(19, "应用裁剪", Row, 76, Editable && Selected))
	{
		if(m_Project.Trim(m_Selected, m_Project.FirstTick() + m_InInput.GetInteger(), m_Project.FirstTick() + m_OutInput.GetInteger())) Changed();
	}
	if(Button(20, "- 速度", Row, 64, Editable && Selected)) { m_Project.SetSpeed(m_Selected, std::max(25, m_Project.Clips()[m_Selected].m_Speed - 25)); Changed(); }
	if(Button(21, "+ 速度", Row, 64, Editable && Selected)) { m_Project.SetSpeed(m_Selected, std::min(400, m_Project.Clips()[m_Selected].m_Speed + 25)); Changed(); }
	Ui()->DoLabel(&Row, "入/出点单位：Tick（50 = 1 秒）", 11, TEXTALIGN_MR);
	Panel.HSplitTop(8, nullptr, &Panel);
	Panel.HSplitTop(20, &Row, &Panel);
	Ui()->DoLabel(&Row, "成片时间轴  /  点击定位 · 拖动片段排序 · 拖动边缘裁剪（吸附到 Tick）", 12, TEXTALIGN_ML);
	CUIRect Track;
	Panel.HSplitTop(66, &Track, &Panel);
	Track.Draw(ColorRGBA(0.035f, 0.04f, 0.05f, 1), IGraphics::CORNER_ALL, 4);
	const double Duration = std::max(0.02, m_Project.Duration());
	const float Pixels = Track.w * m_Zoom / Duration;
	const float Offset = m_Scroll * Track.w * (m_Zoom - 1);
	Ui()->ClipEnable(&Track);
	for(int I = 0; I < (int)m_Project.Clips().size(); ++I)
	{
		const auto &Clip = m_Project.Clips()[I];
		CUIRect Rect = Track;
		Rect.x += m_Project.ClipStart(I) * Pixels - Offset;
		Rect.w = std::max(1.0f, (float)m_Project.Duration(I) * Pixels - 2);
		Rect.y += 17; Rect.h -= 20;
		const bool IsSelected = I == m_Selected;
		Rect.Draw(IsSelected ? ColorRGBA(0.32f, 0.36f, 0.39f, 1) : ColorRGBA(0.17f, 0.19f, 0.22f, 1), IGraphics::CORNER_ALL, 3);
		CUIRect Label = Rect;
		Label.Margin(6, &Label);
		SLabelProperties Props;
		Props.m_MaxWidth = std::max(0.0f, Label.w); Props.m_EllipsisAtEnd = true;
		char aText[128];
		str_format(aText, sizeof(aText), "%02d  %s   %d%%", I + 1, Clip.m_Name.c_str(), Clip.m_Speed);
		if(Rect.w > 38) Ui()->DoLabel(&Label, aText, 12, TEXTALIGN_ML, Props);
		CUIRect Ruler = Rect; Ruler.y = Track.y; Ruler.h = 16;
		Ui()->DoLabel(&Ruler, TimeLabel(m_Project.ClipStart(I)).c_str(), 10, TEXTALIGN_ML);
		if(IsSelected)
		{
			CUIRect Handle = Rect; Handle.w = 3; Handle.Draw(ColorRGBA(0.8f, 0.82f, 0.85f, 1), 0, 0);
			Handle.x = Rect.x + Rect.w - 3; Handle.Draw(ColorRGBA(0.8f, 0.82f, 0.85f, 1), 0, 0);
		}
		if(Editable && m_DragIndex < 0 && Ui()->MouseInside(&Track) && Ui()->MouseInside(&Rect) && Ui()->MouseButtonClicked(0))
		{
			Select(I);
			Seek(Clip.m_In);
			DemoPlayer()->Pause();
			m_DragIndex = I; m_DragStartX = Ui()->MouseX();
			m_DragIn = Clip.m_In; m_DragOut = Clip.m_Out;
			m_DragKind = Ui()->MouseX() - Rect.x < 6 ? 1 : Rect.x + Rect.w - Ui()->MouseX() < 6 ? 2 : 3;
		}
	}
	CUIRect Playhead = Track;
	Playhead.x += m_Cursor * Pixels - Offset; Playhead.w = 2;
	Playhead.Draw(ColorRGBA(0.94f, 0.73f, 0.43f, 1), 0, 0);
	if(m_DragIndex >= 0)
	{
		Ui()->SetActiveItem(&m_DragIndex);
		CUIRect Guide = Track;
		Guide.x = Ui()->MouseX(); Guide.w = 2;
		Guide.Draw(ColorRGBA(0.85f, 0.87f, 0.9f, 0.8f), 0, 0);
		if(!Ui()->MouseButton(0))
		{
			const float Delta = Ui()->MouseX() - m_DragStartX;
			if(Editable && std::abs(Delta) > 3 && m_DragIndex < (int)m_Project.Clips().size())
			{
				const int Index = m_DragIndex;
				if(m_DragKind == 3)
				{
					const auto Pos = m_Project.Position((Ui()->MouseX() - Track.x + Offset) / Pixels);
					if(m_Project.Move(Index, Pos.m_Clip)) m_Selected = Pos.m_Clip;
				}
				else
				{
					const int Ticks = (int)std::round(Delta / Pixels * 50 * m_Project.Clips()[Index].m_Speed / 100.0);
					m_Project.Trim(Index, m_DragKind == 1 ? std::clamp(m_DragIn + Ticks, m_Project.FirstTick(), m_DragOut - 1) : m_DragIn,
						m_DragKind == 2 ? std::clamp(m_DragOut + Ticks, m_DragIn + 1, m_Project.LastTick()) : m_DragOut);
				}
				Changed();
			}
			m_DragIndex = -1;
			Ui()->SetActiveItem(nullptr);
		}
	}
	Ui()->ClipDisable();
	Panel.HSplitTop(13, &Row, &Panel);
	m_Scroll = Ui()->DoScrollbarH(&m_ScrollSlider, &Row, m_Scroll);
	Panel.HSplitTop(4, nullptr, &Panel);
	Panel.HSplitTop(22, &Row, &Panel);
	if(Button(22, m_RemoveChat ? "Demo 聊天：移除" : "Demo 聊天：保留", Row, 130, Editable)) m_RemoveChat = !m_RemoveChat;
#if defined(CONF_VIDEORECORDER)
	if(Button(23, g_Config.m_ClVideoShowhud ? "HUD：开" : "HUD：关", Row, 74, Editable)) g_Config.m_ClVideoShowhud ^= 1;
	if(Button(24, g_Config.m_ClVideoSndEnable ? "音频：开" : "音频：关", Row, 74, Editable)) g_Config.m_ClVideoSndEnable ^= 1;
	if(Button(25, g_Config.m_ClVideoShowChat ? "聊天：开" : "聊天：关", Row, 74, Editable)) g_Config.m_ClVideoShowChat ^= 1;
	char aVideo[64]; str_format(aVideo, sizeof(aVideo), "MP4: %d FPS / CRF %d", g_Config.m_ClVideoRecorderFPS, g_Config.m_ClVideoX264Crf);
	if(Button(26, aVideo, Row, 166, Editable)) g_Config.m_ClVideoRecorderFPS = g_Config.m_ClVideoRecorderFPS == 60 ? 30 : 60;
#endif
	if(m_Selected >= 0 && m_Selected < (int)m_Project.Clips().size())
	{
		const auto &Clip = m_Project.Clips()[m_Selected];
		Ui()->DoLabel(&Row, ("选中 " + std::to_string(m_Selected + 1) + "  " + TimeLabel((Clip.m_In - m_Project.FirstTick()) / 50.0) + " → " + TimeLabel((Clip.m_Out - m_Project.FirstTick()) / 50.0)).c_str(), 11, TEXTALIGN_MR);
	}
	Panel.HSplitTop(20, &Row, &Panel);
	SLabelProperties Props; Props.m_MaxWidth = Row.w; Props.m_EllipsisAtEnd = true;
	std::string Status = m_Status;
	if(m_pExport) Status = "Demo 导出进度：" + std::to_string(m_pExport->Completed()) + " / " + std::to_string(m_Project.Clips().size());
	Ui()->DoLabel(&Row, Status.c_str(), 11, TEXTALIGN_ML, Props);
	TextRender()->TextColor(OldColor);
}
