#include <game/client/components/menus.h>
#include <base/str.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/steam.h>
#include <game/client/animstate.h>
#include <game/client/components/tclient/ai_reply.h>
#include <game/client/components/tclient/video_file_dialog.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <algorithm>
#include <string>
#include <vector>

void CMenus::RenderSettingsXyrion(CUIRect MainView)
{
	CUIRect Tabs;
	MainView.HSplitTop(26.0f, &Tabs, &MainView);
	MainView.HSplitTop(10.0f, nullptr, &MainView);
	CUIRect BrowserButton;
	Tabs.VSplitRight(130.0f, &Tabs, &BrowserButton);
	Tabs.VSplitRight(8.0f, &Tabs, nullptr);
	static CButtonContainer s_BrowserButton;
	if(DoButton_Menu(&s_BrowserButton, "内置浏览器", 0, &BrowserButton))
		GameClient()->m_WebBrowser.Toggle();
	static CButtonContainer s_aTabs[4];
	const char *apTabs[] = {"游戏", "视觉", "Mod", "Qm 功能"};
	const float TabWidth = Tabs.w / 4.0f;
	for(int i = 0; i < 4; ++i)
	{
		CUIRect Button; Tabs.VSplitLeft(TabWidth, &Button, &Tabs);
		if(DoButton_MenuTab(&s_aTabs[i], apTabs[i], g_Config.m_XcSettingsTab == i, &Button, IGraphics::CORNER_ALL))
		{
			if(g_Config.m_XcSettingsTab != i) { Ui()->ClosePopupMenus(); Ui()->SetActiveItem(nullptr); }
			g_Config.m_XcSettingsTab = i;
		}
	}
	if(g_Config.m_XcSettingsTab == 2) { RenderModsEditorWindow(MainView); return; }
	static CScrollRegion s_aScrollRegions[3];
	auto &Scroll = s_aScrollRegions[g_Config.m_XcSettingsTab == 3 ? 2 : g_Config.m_XcSettingsTab];
	CScrollRegionParams Params;
	Params.m_ScrollUnit = 60.0f; Params.m_ForceShowScrollbar = true;
	Scroll.Begin(&MainView, &Params);
	MainView.VMargin(10.0f, &MainView);
	CUIRect Left, Right, Column;
	const bool Narrow = MainView.w < 640.0f;
	MainView.VSplitMid(&Left, &Right, 20.0f);
	CUIRect &First = Narrow ? MainView : Left;
	CUIRect &Second = Narrow ? MainView : Right;
	const auto Section = [&](CUIRect &Side, const char *pTitle, int Rows) {
		CUIRect Box, Heading;
		Side.HSplitTop(44.0f + Rows * 29.0f, &Box, &Side);
		Side.HSplitTop(15.0f, nullptr, &Side);
		Scroll.AddRect(Box);
		Box.Draw(ColorRGBA(0, 0, 0, 0.25f), IGraphics::CORNER_ALL, 10.0f);
		Box.Margin(10.0f, &Column);
		Column.HSplitTop(24.0f, &Heading, &Column);
		Ui()->DoLabel(&Heading, pTitle, 18.0f, TEXTALIGN_ML);
	};
	const auto Row = [&]() { CUIRect Rect; Column.HSplitTop(24.0f, &Rect, &Column); Column.HSplitTop(5.0f, nullptr, &Column); return Rect; };
	const auto Toggle = [&](int &Value, const char *pText) {
		const auto Rect = Row();
		if(!DoButton_CheckBox(&Value, pText, Value, &Rect)) return false;
		Value ^= 1; return true;
	};
	const auto Slider = [&](int &Value, const char *pText, int Min, int Max, const char *pUnit = "") {
		const auto Rect = Row(); Ui()->DoScrollbarOption(&Value, &Value, &Rect, pText, Min, Max, &CUi::ms_LinearScrollbarScale, 0, pUnit);
	};
	const auto Color = [&](unsigned &Value, const char *pText) {
		CUIRect Rect = Row(), Picker; Rect.VSplitRight(38.0f, &Rect, &Picker);
		Ui()->DoLabel(&Rect, pText, 13.0f, TEXTALIGN_ML); DoButton_ColorPicker(&Picker, &Value, false);
	};
	const auto Edit = [&](CLineInput &Input, const char *pLabel, char *pBuffer, size_t Size, const char *pPlaceholder) {
		CUIRect Rect = Row(), Caption; Rect.VSplitLeft(std::min(95.0f, Rect.w * 0.35f), &Caption, &Rect);
		Ui()->DoLabel(&Caption, pLabel, 13.0f, TEXTALIGN_ML);
		Input.SetBuffer(pBuffer, Size); Input.SetEmptyText(pPlaceholder); return Ui()->DoEditBox(&Input, &Rect, 13.0f);
	};
	if(g_Config.m_XcSettingsTab == 3)
	{
		Section(First, "武器弹道辅助线", 6);
		{
			CUIRect Rect = Row();
			static CButtonContainer s_aTrajectoryModes[3];
			const char *apModes[] = {"关闭", "按键显示", "始终显示"};
			const float Width = Rect.w / 3.0f;
			for(int i = 0; i < 3; ++i)
			{
				CUIRect Button; Rect.VSplitLeft(Width, &Button, &Rect);
				if(DoButton_MenuTab(&s_aTrajectoryModes[i], apModes[i], g_Config.m_QmWeaponTrajectory == i, &Button, IGraphics::CORNER_ALL))
					g_Config.m_QmWeaponTrajectory = i;
			}
		}
		Toggle(g_Config.m_QmWeaponTrajectoryGun, "手枪辅助线");
		Toggle(g_Config.m_QmWeaponTrajectoryNinja, "忍者路径预测");
		Color(g_Config.m_QmWeaponTrajectoryColor, "辅助线颜色");
		Slider(g_Config.m_QmWeaponTrajectoryWidth, "辅助线宽度", 1, 10);
		Slider(g_Config.m_QmWeaponTrajectoryAlpha, "辅助线不透明度", 0, 100, "%");

		Section(First, "Gores 队伍落水统计", 4);
		if(Toggle(g_Config.m_QmGoresDrownBoard, "启用队伍落水统计")) GameClient()->m_TClient.ResetGoresDrownCounts();
		Toggle(g_Config.m_QmGoresDrownBoardShowTee, "榜单显示 Tee");
		Slider(g_Config.m_QmGoresDrownBoardMaxPlayers, "最多显示人数", 1, 16);
		Slider(g_Config.m_QmGoresDrownBoardOpacity, "榜单不透明度", 0, 100, "%");

		Section(First, "分身小窗", 4);
		Toggle(g_Config.m_QmDummyMiniView, "启用分身小窗");
		Toggle(g_Config.m_QmDummyMiniViewAuto, "仅在分身离开视野时显示");
		Slider(g_Config.m_QmDummyMiniViewSize, "小窗大小", 50, 200, "%");
		Slider(g_Config.m_QmDummyMiniViewZoom, "小窗缩放", 10, 300, "%");

		Section(First, "Steam 集成", 3);
		Toggle(g_Config.m_QmSteamAutoLaunch, "启动客户端时自动打开 Steam");
		static CLineInput s_SteamPathInput;
		Edit(s_SteamPathInput, "Steam 路径", g_Config.m_QmSteamClientPath, sizeof(g_Config.m_QmSteamClientPath), "自动检测");
		{ const auto Rect = Row(); static CButtonContainer s_OpenSteam; if(DoButton_Menu(&s_OpenSteam, "打开 Steam", 0, &Rect)) SteamOpenClient(); }

		Section(Second, "聊天与输入法", 5);
		Toggle(g_Config.m_QmChatCommandCompletion, "启用 Qm 聊天指令补全");
		Toggle(g_Config.m_QmNewIme, "启用 Qm 输入法候选栏");
		Toggle(g_Config.m_QmImeAutoManage, "跟随文本焦点切换输入法");
		Slider(g_Config.m_QmImeFontSize, "候选栏字号", 75, 200, "%");
		Slider(g_Config.m_QmImeOpacity, "候选栏不透明度", 0, 100, "%");

		Section(Second, "Qm 界面与动画", 4);
		{
			CUIRect Rect = Row();
			static CButtonContainer s_aMotionModes[3];
			const char *apModes[] = {"关闭动画", "减少动画", "完整动画"};
			const float Width = Rect.w / 3.0f;
			for(int i = 0; i < 3; ++i)
			{
				CUIRect Button; Rect.VSplitLeft(Width, &Button, &Rect);
				if(DoButton_MenuTab(&s_aMotionModes[i], apModes[i], g_Config.m_QmUiMotionLevel == i, &Button, IGraphics::CORNER_ALL))
					g_Config.m_QmUiMotionLevel = i;
			}
		}
		Toggle(g_Config.m_QmGaussianBlur, "启用 Qm 背景模糊");
		Toggle(g_Config.m_QmTooltipAnimation, "提示气泡动画");
		Slider(g_Config.m_QmUiScale, "界面缩放", 50, 200, "%");

		Section(Second, "控制台外观", 4);
		Toggle(g_Config.m_QmConsoleHighlightCommands, "高亮控制台命令");
		Slider(g_Config.m_QmConsoleFontSize, "控制台字号", 1, 24);
		Slider(g_Config.m_QmConsoleOpacity, "控制台不透明度", 20, 100, "%");
		{
			CUIRect Rect = Row();
			static CButtonContainer s_aSchemes[3];
			const char *apSchemes[] = {"现代终端", "PowerShell", "自定义"};
			const float Width = Rect.w / 3.0f;
			for(int i = 0; i < 3; ++i)
			{
				CUIRect Button; Rect.VSplitLeft(Width, &Button, &Rect);
				if(DoButton_MenuTab(&s_aSchemes[i], apSchemes[i], g_Config.m_QmConsoleColorScheme == i, &Button, IGraphics::CORNER_ALL))
					g_Config.m_QmConsoleColorScheme = i;
			}
		}
	}
	else if(g_Config.m_XcSettingsTab == 0)
	{
		Section(First, "内置浏览器", 2);
		static CLineInput s_BrowserUrlInput;
		Edit(s_BrowserUrlInput, "网页地址", g_Config.m_XcBrowserUrl, sizeof(g_Config.m_XcBrowserUrl), "https://www.bing.com");
		{ const auto Rect = Row(); static CButtonContainer s_OpenBrowserButton; if(DoButton_Menu(&s_OpenBrowserButton, "打开浏览器", 0, &Rect)) GameClient()->m_WebBrowser.Toggle(); }
		Section(First, "输入预测", 8);
		{
			CUIRect Rect = Row();
			static CButtonContainer s_aInputModes[7];
			const char *apModes[] = {"关", "Fast", "Best", "Saiko", "Delta", "F", "Cloud"};
			const float ButtonWidth = Rect.w / 7.0f;
			for(int i = 0; i < 7; ++i)
			{
				CUIRect Button; Rect.VSplitLeft(ButtonWidth, &Button, &Rect);
				const bool Selected = g_Config.m_BcInputs == i || (i == BC_INPUTS_FAST && g_Config.m_BcInputs == BC_INPUTS_OFF && g_Config.m_TcFastInput);
				if(DoButton_MenuTab(&s_aInputModes[i], apModes[i], Selected, &Button, IGraphics::CORNER_ALL))
				{
					g_Config.m_BcInputs = i;
					g_Config.m_TcFastInput = i == BC_INPUTS_FAST;
				}
			}
		}
		if(g_Config.m_BcInputs == BC_INPUTS_FAST)
		{
			Slider(g_Config.m_TcFastInputAmount, "预测提前量", 1, 100, " ms");
			Toggle(g_Config.m_TcFastInputOthers, "对其他玩家预测");
		}
		else if(g_Config.m_BcInputs == BC_INPUTS_BEST)
		{
			Slider(g_Config.m_BcBestInputAmount, "预测提前量", 0, 1000, " ×0.01 tick");
			Slider(g_Config.m_BcBestInputSmoothing, "平滑", 0, 100, "%");
			Slider(g_Config.m_BcBestInputLatencyComp, "延迟补偿", 0, 50, "%");
			{
				CUIRect Rect = Row(); static CButtonContainer s_aInterpolation[3];
				const char *apInterpolation[] = {"线性", "曲线", "自适应"};
				const float Width = Rect.w / 3.0f;
				for(int i = 0; i < 3; ++i)
				{
					CUIRect Button; Rect.VSplitLeft(Width, &Button, &Rect);
					if(DoButton_MenuTab(&s_aInterpolation[i], apInterpolation[i], g_Config.m_BcBestInputInterpolation == i + 1, &Button, IGraphics::CORNER_ALL))
						g_Config.m_BcBestInputInterpolation = i + 1;
				}
			}
			Toggle(g_Config.m_BcBestInputOthers, "对其他玩家预测");
		}
		else if(g_Config.m_BcInputs == BC_INPUTS_SAIKO || g_Config.m_BcInputs == BC_INPUTS_DELTA || g_Config.m_BcInputs == BC_INPUTS_F || g_Config.m_BcInputs == BC_INPUTS_CLOUD)
		{
			int *pAmount = g_Config.m_BcInputs == BC_INPUTS_SAIKO ? &g_Config.m_BcSaikoInputAmount :
				g_Config.m_BcInputs == BC_INPUTS_DELTA ? &g_Config.m_BcDeltaInputAmount :
				g_Config.m_BcInputs == BC_INPUTS_F ? &g_Config.m_BcFInputAmount : &g_Config.m_BcCloudInputAmount;
			const int Max = g_Config.m_BcInputs == BC_INPUTS_F ? 5000 : (g_Config.m_BcInputs == BC_INPUTS_SAIKO ? 500 : (g_Config.m_BcInputs == BC_INPUTS_DELTA ? 500 : 500));
			Slider(*pAmount, "预测提前量", 0, Max, g_Config.m_BcInputs == BC_INPUTS_F ? " ×0.001 tick" : " ×0.01 tick");
			int *pOthers = g_Config.m_BcInputs == BC_INPUTS_SAIKO ? &g_Config.m_BcSaikoInputOthers :
				g_Config.m_BcInputs == BC_INPUTS_DELTA ? &g_Config.m_BcDeltaInputOthers :
				g_Config.m_BcInputs == BC_INPUTS_F ? &g_Config.m_BcFInputOthers : &g_Config.m_BcCloudInputOthers;
			Toggle(*pOthers, "对其他玩家预测");
		}
		Toggle(g_Config.m_ClSubTickAiming, "Sub-Tick 瞄准");
		if(!g_Config.m_ClSubTickAiming) Toggle(g_Config.m_TcFastInputRepredictHook, "钩子方向改变时重新预测"); else Row();
		Toggle(g_Config.m_TcMovingTilesEntities, "实体层显示移动图块");
		Section(First, "Snap Tap", 2);
		Toggle(g_Config.m_BcSnapTap, "开启反向键优先");
		if(g_Config.m_BcSnapTap) Slider(g_Config.m_BcSnapTapDelay, "切换延迟", 0, 200, " ms"); else Row();
		Section(First, "移动方块预测", 1);
		if(Toggle(g_Config.m_XcAxiomQuadCollision, "移动方块预测")) GameClient()->UpdateAxiomQuadCollision();
		Section(First, "跑图预测", 4);
		Toggle(g_Config.m_BcFinishPrediction, "启用跑图预测");
		Toggle(g_Config.m_BcFinishPredictionShowTime, "显示预测时间");
		Toggle(g_Config.m_BcFinishPredictionShowPercentage, "显示完成百分比");
		if(Toggle(g_Config.m_BcFinishPredictionConsiderFreezeWalls, "考虑可跳过的冻结墙")) GameClient()->m_FinishPrediction.RequestPathRebuild();
		Section(Second, "钩救求解", 9);
		{
			auto &Solver = GameClient()->m_RescueSolver;
			if(Toggle(g_Config.m_XcRescueSolver, "启用冻结队友钩救求解")) Solver.Clear();
			Toggle(g_Config.m_XcRescueOverlay, "显示站位、到达路线和瞄准线");
			std::vector<int> vIds = {-1};
			std::vector<std::string> vNames = {"请选择被冻结的队友"};
			int Selected = 0;
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				const bool Candidate = Solver.IsCandidate(Id);
				if(!Candidate && !(Solver.Result().m_Valid && Solver.Target() == Id)) continue;
				if(Solver.Target() == Id) Selected = vIds.size();
				vIds.push_back(Id);
				char aName[128];
				const char *pName = GameClient()->m_aClients[Id].m_aName;
				str_format(aName, sizeof(aName), "[%d] %s%s", Id, pName[0] ? pName : "原救援目标", Candidate ? "" : "（已有标记）");
				vNames.emplace_back(aName);
			}
			std::vector<const char *> vpNames;
			for(const auto &Name : vNames) vpNames.push_back(Name.c_str());
			static CUi::SDropDownState s_RescueTargets;
			CUIRect TargetRect = Row();
			const int NewSelected = Ui()->DoDropDown(&TargetRect, Selected, vpNames.data(), vpNames.size(), s_RescueTargets);
			if(NewSelected >= 0 && NewSelected < (int)vIds.size() && NewSelected != Selected)
				Solver.SelectTarget(vIds[NewSelected]);
			{
				CUIRect Rect = Row(), Solve, Clear;
				Rect.VSplitMid(&Solve, &Clear, 8);
				static CButtonContainer s_RescueSolve, s_RescueClear;
				if(DoButton_Menu(&s_RescueSolve, Solver.Busy() ? "重新求解" : "求解", 0, &Solve)) Solver.Start();
				if(DoButton_Menu(&s_RescueClear, Solver.Busy() ? "取消" : "清除标记", 0, &Clear)) Solver.Clear();
			}
			const auto Label = [&](const char *pText) {
				const auto Rect = Row(); SLabelProperties Props; Props.m_EllipsisAtEnd = true;
				Ui()->DoLabel(&Rect, pText, 12.0f, TEXTALIGN_ML, Props);
			};
			char aText[256];
			if(Solver.Busy()) str_format(aText, sizeof(aText), "%s  %d%%", Solver.Status(), Solver.Progress());
			else str_copy(aText, Solver.Status());
			Label(aText);
			const auto &Result = Solver.Result();
			if(Result.m_Valid && !Solver.Busy())
			{
				str_format(aText, sizeof(aText), "站位（格）：X %.2f    Y %.2f", Result.m_Position.x / 32, Result.m_Position.y / 32); Label(aText);
				str_format(aText, sizeof(aText), "站位（世界）：X %.1f    Y %.1f", Result.m_Position.x, Result.m_Position.y); Label(aText);
				str_format(aText, sizeof(aText), "鼠标角度：%.2f°    %s", Result.m_Angle, Result.m_Grounded ? "地面起钩" : "空中起钩"); Label(aText);
				str_format(aText, sizeof(aText), "瞄准向量：X %d    Y %d", Result.m_MouseX, Result.m_MouseY); Label(aText);
			}
			else
			{
				Label("站位（格）：—"); Label("站位（世界）：—");
				Label("鼠标角度：—"); Label("瞄准向量：—");
			}
		}
		Section(Second, "AI 自动回复", 12);
		bool Changed = Toggle(g_Config.m_XcAiEnabled, "启用 AI 自动回复");
		Changed |= Toggle(g_Config.m_XcAiContext, "记住聊天上下文");
		Changed |= Toggle(g_Config.m_XcAiWebSearch, "联网搜索");
		{
			CUIRect Rect = Row(), OpenAI, Anthropic; Rect.VSplitMid(&OpenAI, &Anthropic, 5.0f);
			static CButtonContainer s_OpenAIButton, s_AnthropicButton;
			if(DoButton_MenuTab(&s_OpenAIButton, "OpenAI", g_Config.m_XcAiProtocol == AiReply::OPENAI, &OpenAI, IGraphics::CORNER_L) && g_Config.m_XcAiProtocol != AiReply::OPENAI) { g_Config.m_XcAiProtocol = AiReply::OPENAI; Changed = true; }
			if(DoButton_MenuTab(&s_AnthropicButton, "Anthropic", g_Config.m_XcAiProtocol == AiReply::ANTHROPIC, &Anthropic, IGraphics::CORNER_R) && g_Config.m_XcAiProtocol != AiReply::ANTHROPIC) { g_Config.m_XcAiProtocol = AiReply::ANTHROPIC; Changed = true; }
		}
		static CLineInput s_EndpointInput, s_KeyInput, s_ModelInput, s_PromptInput;
		const char *pExample = g_Config.m_XcAiProtocol == AiReply::ANTHROPIC ? "https://api.anthropic.com/v1/messages" : "https://api.openai.com/v1/chat/completions";
		Changed |= Edit(s_EndpointInput, "完整接口地址", g_Config.m_XcAiEndpoint, sizeof(g_Config.m_XcAiEndpoint), pExample);
		s_KeyInput.SetHidden(true);
		Changed |= Edit(s_KeyInput, "API 密钥", g_Config.m_XcAiKey, sizeof(g_Config.m_XcAiKey), "");
		Changed |= Edit(s_ModelInput, "模型名称", g_Config.m_XcAiModel, sizeof(g_Config.m_XcAiModel), "");
		Changed |= Edit(s_PromptInput, "系统提示词", g_Config.m_XcAiPrompt, sizeof(g_Config.m_XcAiPrompt), AiReply::DEFAULT_SYSTEM_PROMPT);
		{ const auto Rect = Row(); Changed |= Ui()->DoScrollbarOption(&g_Config.m_XcAiContextTurns, &g_Config.m_XcAiContextTurns, &Rect, "上下文轮数", 1, 32); }
		{ const auto Rect = Row(); Changed |= Ui()->DoScrollbarOption(&g_Config.m_XcAiCooldown, &g_Config.m_XcAiCooldown, &Rect, "回复冷却", 0, 120, &CUi::ms_LinearScrollbarScale, 0, " 秒"); }
		{
			CUIRect Rect = Row(), Button, Count; Rect.VSplitLeft(std::min(110.0f, Rect.w * 0.5f), &Button, &Count);
			static CButtonContainer s_ClearContextButton;
			if(DoButton_Menu(&s_ClearContextButton, "清空上下文", 0, &Button)) GameClient()->m_AiReply.ClearContext();
			char aCount[64]; str_format(aCount, sizeof(aCount), "已保留 %zu 轮", GameClient()->m_AiReply.ContextSize());
			Count.VSplitLeft(8.0f, nullptr, &Count); Ui()->DoLabel(&Count, aCount, 12.0f, TEXTALIGN_ML);
		}
		if(Changed) GameClient()->m_AiReply.OnConfigChanged();
		{ const auto Rect = Row(); char aStatus[256]; str_format(aStatus, sizeof(aStatus), "%s: %s (%zu)", Localize("状态"), Localize(GameClient()->m_AiReply.Status()), GameClient()->m_AiReply.Pending()); SLabelProperties Props; Props.m_EllipsisAtEnd = true; Ui()->DoLabel(&Rect, aStatus, 12.0f, TEXTALIGN_ML, Props); }
	}
	else
	{
		Section(First, "界面与液态玻璃", 8);
		Toggle(g_Config.m_BcAnimations, "UI 动画");
		Toggle(g_Config.m_XcGlobalGlass, "全局 UI 液态玻璃");
		Slider(g_Config.m_XcGlassOpacity, "液态玻璃不透明度", 0, 100, "%");
		Color(g_Config.m_XcGlassColor, "液态玻璃颜色（RGB）");
		{ const auto Rect = Row(); Rect.Draw(ColorRGBA(0, 0, 0, 0.25f), IGraphics::CORNER_ALL, 5.0f); Ui()->DoLabel(&Rect, "实时预览", 13.0f, TEXTALIGN_MC); }
		Toggle(g_Config.m_XcCommandHints, "中文指令提示");
		Toggle(g_Config.m_BcServerMapPreview, "服务器地图预览");
		Toggle(g_Config.m_XcAutoTranslateChinese, "自动翻译为中文");
		Section(First, "屏幕比例", g_Config.m_BcCustomAspectRatioMode == 2 ? 5 : 4);
		{
			const char *apModes[] = {"关闭", "预设", "自定义"}; static CButtonContainer s_aModes[3];
			CUIRect Rect = Row(); const float Width = Rect.w / 3;
			for(int i = 0; i < 3; ++i)
			{
				CUIRect B; Rect.VSplitLeft(Width, &B, &Rect);
				const int Mode = g_Config.m_BcCustomAspectRatioMode < 0 ? (g_Config.m_BcCustomAspectRatio > 0 ? 1 : 0) : g_Config.m_BcCustomAspectRatioMode;
				if(DoButton_MenuTab(&s_aModes[i], apModes[i], Mode == i, &B, IGraphics::CORNER_ALL))
				{
					g_Config.m_BcCustomAspectRatioMode = i;
					if(i && g_Config.m_BcCustomAspectRatio < 100) g_Config.m_BcCustomAspectRatio = 178;
					if(i == 2 && (!g_Config.m_BcCustomAspectRatioNum || !g_Config.m_BcCustomAspectRatioDen)) { g_Config.m_BcCustomAspectRatioNum = 16; g_Config.m_BcCustomAspectRatioDen = 9; }
				}
			}
		}
		{
			CUIRect Rect = Row(); const char *apNames[] = {"5:4", "4:3", "3:2", "16:9", "21:9"}; const int aRatios[] = {125, 133, 150, 178, 233}; static CButtonContainer s_aPresets[5]; const float Width = Rect.w / 5;
			for(int i = 0; i < 5; ++i) { CUIRect B; Rect.VSplitLeft(Width, &B, &Rect); if(DoButton_MenuTab(&s_aPresets[i], apNames[i], g_Config.m_BcCustomAspectRatioMode == 1 && g_Config.m_BcCustomAspectRatio == aRatios[i], &B, IGraphics::CORNER_ALL)) { g_Config.m_BcCustomAspectRatioMode = 1; g_Config.m_BcCustomAspectRatio = aRatios[i]; } }
		}
		{
			CUIRect Rect = Row(); static CButtonContainer s_aApply[3]; const char *apNames[] = {"仅游戏", "全界面", "游戏不含 HUD"}; const float Width = Rect.w / 3;
			for(int i = 0; i < 3; ++i) { CUIRect B; Rect.VSplitLeft(Width, &B, &Rect); if(DoButton_MenuTab(&s_aApply[i], apNames[i], g_Config.m_BcCustomAspectRatioApplyMode == i, &B, IGraphics::CORNER_ALL)) g_Config.m_BcCustomAspectRatioApplyMode = i; }
		}
		{
			if(g_Config.m_BcCustomAspectRatioMode == 2)
			{
				const auto Num = Row(), Den = Row();
				g_Config.m_BcCustomAspectRatioNum = (int)Ui()->DoValueSelector(&g_Config.m_BcCustomAspectRatioNum, &Num, "自定义宽度", g_Config.m_BcCustomAspectRatioNum, 1, 100000);
				g_Config.m_BcCustomAspectRatioDen = (int)Ui()->DoValueSelector(&g_Config.m_BcCustomAspectRatioDen, &Den, "自定义高度", g_Config.m_BcCustomAspectRatioDen, 1, 100000);
				g_Config.m_BcCustomAspectRatio = std::clamp((int)((double)g_Config.m_BcCustomAspectRatioNum * 100 / std::max(1, g_Config.m_BcCustomAspectRatioDen) + 0.5), 100, 1000);
			}
		}
		if(g_Config.m_BcCustomAspectRatioMode != 2) Slider(g_Config.m_BcCustomAspectRatio, "比例 ×100", 100, 1000);
		Section(First, "视频背景", 3);
		Toggle(g_Config.m_XcVideoEnabled, "启用视频背景");
		{
			CUIRect Rect = Row(), Button; Rect.VSplitRight(95.0f, &Rect, &Button); SLabelProperties Props; Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Rect, g_Config.m_XcVideoPath[0] ? g_Config.m_XcVideoPath : "未选择视频", 13.0f, TEXTALIGN_ML, Props);
			static CButtonContainer s_SelectVideo;
			if(DoButton_Menu(&s_SelectVideo, "选择视频…", 0, &Button)) if(auto Path = SelectVideoFile(g_Config.m_XcVideoPath)) str_copy(g_Config.m_XcVideoPath, Path->c_str());
		}
		Slider(g_Config.m_XcVideoDim, "背景暗化", 0, 100, "%");
		Section(Second, "Tee 自定义", 9);
		{
			CUIRect Preview = Row(); Row(); Row(); Preview.h = 82.0f;
			CTeeRenderInfo Info;
			Info.Apply(GameClient()->m_Skins.Find(g_Config.m_ClDummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin));
			Info.ApplyColors(g_Config.m_ClDummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor,
				g_Config.m_ClDummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody,
				g_Config.m_ClDummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet);
			if(Client()->IsSixup())
			{
				for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
				{
					GameClient()->m_Skins7.FindSkinPart(Part, CSkins7::ms_apSkinVariables[g_Config.m_ClDummy][Part], false)->ApplyTo(Info.m_aSixup[g_Config.m_ClDummy]);
					GameClient()->m_Skins7.ApplyColorTo(Info.m_aSixup[g_Config.m_ClDummy], *CSkins7::ms_apUCCVariables[g_Config.m_ClDummy][Part], *CSkins7::ms_apColorVariables[g_Config.m_ClDummy][Part], Part);
				}
			}
			Info.m_Size = 52.0f;
			if(Client()->State() == IClient::STATE_ONLINE && GameClient()->m_Snap.m_LocalClientId >= 0) { Info = GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_RenderInfo; Info.m_Size = 52.0f; }
			const vec2 TeePosition = Preview.Center() + vec2(0, 10);
			const vec2 Delta = Ui()->MousePos() - TeePosition;
			const vec2 Direction = length(Delta) > 0.001f ? normalize(Delta) : vec2(1, 0);
			RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, EMOTE_NORMAL, Direction, TeePosition, 1.0f, true);
		}
		{ const auto Rect = Row(); static CButtonContainer s_Hat; if(DoButton_Menu(&s_Hat, g_Config.m_XcTeeHat ? "帽子：开启" : "帽子：关闭", 0, &Rect)) g_Config.m_XcTeeHat = g_Config.m_XcTeeHat ? 0 : 1; }
		Slider(g_Config.m_XcTeeHat, "头饰：0无 1帽 2礼帽 3贝雷 4皇冠 5猫耳 6兔耳", 0, 6);
		Color(g_Config.m_XcTeeHatColor, "帽子颜色");
		{ const auto Rect = Row(); static CButtonContainer s_Glasses; if(DoButton_Menu(&s_Glasses, g_Config.m_XcTeeGlasses ? "眼镜：开启" : "眼镜：关闭", 0, &Rect)) g_Config.m_XcTeeGlasses = g_Config.m_XcTeeGlasses ? 0 : 1; }
		Slider(g_Config.m_XcTeeGlasses, "眼镜：0无 1墨镜 2圆框 3细框 4单片", 0, 4);
		Color(g_Config.m_XcTeeGlassesColor, "眼镜颜色");
		Section(Second, "灵动岛", 4);
		Toggle(g_Config.m_BcMusicPlayer, "启用灵动岛");
		Toggle(g_Config.m_BcMusicPlayerShowLyrics, "显示歌词");
		Toggle(g_Config.m_BcMusicPlayerIdleClock, "空闲时显示本地时间");
		Toggle(g_Config.m_BcMusicPlayerGlass, "玻璃胶囊效果");
		Section(Second, "聊天气泡", 3);
		Toggle(g_Config.m_BcChatBubbles, "启用聊天气泡"); Toggle(g_Config.m_BcChatBubblesSelf, "显示自己的气泡"); Toggle(g_Config.m_BcChatBubbleAnimation, "气泡动画");
	}
	Scroll.End();
}
