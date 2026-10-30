#ifndef GAME_CLIENT_COMPONENTS_DEMO_TIMELINE_H
#define GAME_CLIENT_COMPONENTS_DEMO_TIMELINE_H

#include <engine/console.h>
#include <engine/shared/demo_project.h>
#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <memory>
#include <string>

class CDemoProjectExport;

class CDemoTimeline : public CComponent
{
	demo_edit::CProject m_Project;
	bool m_Open = false, m_Playing = false, m_Rendering = false, m_PendingVideo = false;
	int m_Selected = -1, m_In = 0, m_Out = 0, m_PlayClip = -1;
	int64_t m_PlayStart = 0, m_VideoStart = 0;
	double m_PlayOffset = 0, m_Cursor = 0;
	float m_OldSpeed = 1, m_Zoom = 1, m_Scroll = 0;
	int m_OldPauseWithDemo = 0;
	std::string m_Status = "I / O 标记选区，添加到时间轴后预览或导出";
	std::string m_VideoTemp, m_VideoFinal;
	std::shared_ptr<CDemoProjectExport> m_pExport;
	CLineInputBuffered<64> m_Name;
	CLineInputNumber m_InInput, m_OutInput;
	CButtonContainer m_aButtons[40];
	char m_aClipIds[demo_edit::CProject::MAX_CLIPS]{};
	char m_SourceSlider = 0, m_ScrollSlider = 0;
	int m_DragIndex = -1, m_DragKind = 0;
	float m_DragStartX = 0;
	int m_DragIn = 0, m_DragOut = 0;
	bool m_RemoveChat = false;
	bool EnsureSource();
	bool Busy() const;
	void Message(const std::string &Text);
	void Select(int Index);
	void Changed();
	void Seek(int Tick);
	void Preview();
	void StopPreview();
	void FinishVideo(bool Success);
	void Save();
	void Load();
	void Export(bool Video);
	void StartVideo();
	bool ValidName() const;
	bool Button(int Id, const char *pText, CUIRect &Row, float Width, bool Enabled = true);
	static void ConAction(IConsole::IResult *pResult, void *pUser);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnShutdown() override;
	void OnWindowResize() override { FinishVideo(false); }
	bool OnInput(const IInput::CEvent &Event) override;
	void Toggle();
	bool IsOpen() const { return m_Open; }
	bool IsRendering() const { return m_Rendering; }
	void Render(CUIRect Screen);
};
#endif
