#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_WEB_BROWSER_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_WEB_BROWSER_H

#include <game/client/component.h>
#include <engine/console.h>
#include <memory>

class CWebBrowser : public CComponent
{
	class CImpl;
	std::shared_ptr<CImpl> m_pImpl;
	bool m_RestoreFullscreen = false;
	bool m_WasVisible = false;
	bool m_WasInteractive = false;
	static void ConToggle(IConsole::IResult *pResult, void *pUserData);

public:
	CWebBrowser();
	~CWebBrowser() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnUpdate() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnShutdown() override;
	void Toggle();
};

#endif
