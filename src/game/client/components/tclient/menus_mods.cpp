#include <game/client/components/menus.h>
#include <game/client/components/tclient/modsystem/mod_manager.h>
#include <game/client/gameclient.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiSurfaceText.h>
#include <game/client/ui_scrollregion.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/shared/config.h>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace
{
namespace ms = modsystem;
struct SWidget { CButtonContainer m_Button; CLineInputBuffered<4097> m_Input; CUi::SDropDownState m_Drop; };
struct SDrag
{
	int m_Source = 0; // 1 新块，2 整棵子树，3 值片，4 运算符
	ms::CScriptNode m_Node;
	ms::TId m_Id = 0, m_Mod = 0, m_Rule = 0;
	std::string m_Text, m_Label;
	bool m_Holding = false, m_Picked = false;
	bool Active() const { return m_Source != 0; }
	bool Block() const { return m_Source == 1 || m_Source == 2; }
	void Cancel() { m_Source = 0; m_Holding = false; }
};
struct SEditor
{
	ms::TId m_Mod = 0, m_Rule = 0, m_Delete = 0, m_Reload = 0, m_Seen = 0;
	bool m_Editing = false, m_Parameters = false, m_Transfer = false;
	std::string m_Status, m_Rename, m_Path, m_NewName, m_Search;
	std::string m_Help = "从左侧拿起积木，再拖到右侧。Esc / 右键 / 中键取消。";
	SDrag m_Drag;
	std::map<ms::TId, bool> m_Expanded;
	std::map<std::string, std::unique_ptr<SWidget>> m_Widgets;
	CScrollRegion m_Manager, m_Palette, m_Values, m_Canvas;
	SWidget &Widget(const std::string &Key)
	{
		auto &p = m_Widgets[Key]; if(!p) p = std::make_unique<SWidget>(); return *p;
	}
	void Select(ms::TId Mod, ms::TId Rule = 0) { m_Mod = Mod; m_Rule = Rule; m_Drag.Cancel(); m_Delete = m_Reload = 0; }
};
struct SZone
{
	CUIRect m_Rect, m_Indicator;
	ms::SSequenceTarget m_Target;
	size_t m_Index = 0;
	std::string *m_pField = nullptr;
	size_t m_Begin = 0, m_End = 0;
	ms::EParamKind m_Kind = ms::PARAM_NONE;
	bool m_Hover = false;
};
SEditor s_Editor;
ColorRGBA BlockAccent(ms::CScriptNode::EKind Kind)
{
	switch(Kind)
	{
	case ms::CScriptNode::K_IF_ELSE: return ui_token::color::WARNING.WithAlpha(0.65f);
	case ms::CScriptNode::K_LOOP: return ColorRGBA(0.72f, 0.65f, 0.80f, 0.65f);
	case ms::CScriptNode::K_WAIT: return ui_token::color::TEXT_SECONDARY;
	default: return ui_token::color::SUCCESS.WithAlpha(0.65f);
	}
}
CScrollRegionParams ScrollParams(const SUiTheme &Theme)
{
	auto P = QmScrollRegionParamsForSize(EQmScrollSize::MEDIUM);
	P.m_ScrollUnit = 64;
	P.m_RailBgColor = Theme.m_TextBody.WithAlpha(0.06f);
	P.m_pSliderThemeColor = &Theme.m_TextSmall;
	return P;
}
ms::CRule ExampleRule(int Example)
{
	ms::CRule Rule;
	auto Action = [](ms::EAction Type, const std::string &Argument = "") {
		ms::CScriptNode Node; Node.m_Block.m_Action = Type; Node.m_Block.m_Argument = Argument; return Node;
	};
	if(Example == 0)
	{
		Rule.m_Hook = ms::HOOK_ON_KEY_PRESS; Rule.m_Argument = "g";
		Rule.m_vBody.push_back(Action(ms::ACT_ECHO, "已按下 G · 这里可以换成自己的提示文字"));
	}
	else
	{
		Rule.m_Hook = ms::HOOK_ON_SPAWN;
		ms::CScriptNode Wait; Wait.m_Kind = ms::CScriptNode::K_WAIT; Wait.m_Count = "50";
		Rule.m_vBody = {Wait, Action(ms::ACT_ECHO, "出生后等待了 50 tick")};
	}
	return Rule;
}
}

void CMenus::RenderModsEditorWindow(CUIRect MainView)
{
	auto &E = s_Editor; auto &System = GameClient()->m_ModSystem;
	auto Context = SettingsUiContext("xyrion/mods", 1.0f);
	SUiTheme Theme = *Context.m_pTheme;
	Theme.m_Selected = ui_token::color::LIST_ITEM_SELECTED;
	Theme.m_Border = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_QmUiCardBorderColor, true));
	Context.m_pTheme = &Theme;
	const CUiScopedSurfaceText SurfaceText(TextRender(), Theme.m_Surface);
	const auto Surface = [&](const CUIRect &Rect, ColorRGBA Fill, float Radius = ui_token::radius::BASE) {
		DrawRoundedSurface(Context, Rect, Fill, Theme.m_Border, Radius, 1.0f);
	};
	const auto Card = [&](CUIRect &Rect) {
		Surface(Rect, Theme.m_Surface, ui_token::radius::CARD);
		Rect.Margin(ui_token::spacing::SM, &Rect);
	};
	const auto DrawBlock = [&](const CUIRect &Rect, ms::CScriptNode::EKind Kind, const std::string &Key) {
		ui_widget::SButtonSurfaceOptions Options;
		Options.m_Role = EUiButtonRole::LIST_ENTRY;
		Options.m_Color = BlendUiButtonSurface(Theme.m_Surface, Theme.m_TextBody.WithAlpha(0.04f));
		Options.m_Radius = ui_token::radius::BASE;
		ui_widget::DrawButtonSurface(Context, &E.Widget(Key).m_Button, Rect, Options);
		CUIRect Accent = {Rect.x + 3, Rect.y + 8, 2, std::max(0.0f, Rect.h - 16)};
		Accent.Draw(BlockAccent(Kind), IGraphics::CORNER_ALL, 1);
	};
	std::function<void()> Pending;
	std::vector<SZone> Zones;
	E.m_Drag.m_Picked = false;
	if(E.m_Drag.Active() && (Input()->KeyPress(KEY_ESCAPE) || Ui()->MouseButtonClicked(1) || Ui()->MouseButtonClicked(2))) E.m_Drag.Cancel();
	Surface(MainView, Theme.m_Surface, ui_token::radius::CARD);
	MainView.Margin(ui_token::settings::CARD_PADDING, &MainView);
	CUIRect Header, Toolbar, Metadata, Transfer, Footer;
	MainView.HSplitTop(30, &Header, &MainView); MainView.HSplitTop(26, &Toolbar, &MainView);
	MainView.HSplitTop(6, nullptr, &MainView); MainView.HSplitTop(26, &Metadata, &MainView);
	MainView.HSplitBottom(54, &MainView, &Footer); MainView.HSplitTop(4, nullptr, &MainView);
	CUIRect Title, Hint; Header.VSplitLeft(Header.w * 0.40f, &Title, &Hint);
	Ui()->DoLabel(&Title, E.m_Editing ? "积木编辑器" : "Mod 管理器", 19, TEXTALIGN_ML);
	Ui()->DoLabel(&Hint, E.m_Editing ? "拖放编程 · Ctrl+S 保存" : "选择模组，或从下方示例开始", 11, TEXTALIGN_MR);
	auto Button = [&](const std::string &Key, const char *pText, CUIRect Rect, bool Selected = false) {
		Rect.Margin(1, &Rect);
		return ui_widget::SecondaryButton(Context, &E.Widget(Key).m_Button, pText, Rect, E.m_Drag.Active(), EUiButtonGeometry::CONTROL, Selected);
	};
	auto Edit = [&](const std::string &Key, std::string &Value, CUIRect Rect) {
		Rect.Margin(1, &Rect);
		if(E.m_Drag.Active()) { Surface(Rect, Theme.m_InputSurface); Ui()->DoLabel(&Rect, Value.c_str(), 12, TEXTALIGN_ML); return false; }
		auto &Field = E.Widget(Key).m_Input; if(!Field.IsActive() && Value != Field.GetString()) Field.Set(Value.c_str());
		ui_widget::SInputFieldOptions Options;
		Options.m_pPlaceholder = Field.GetEmptyText();
		Options.m_Mode = Key == "palette_search" ? ui_widget::EInputFieldMode::SEARCH : ui_widget::EInputFieldMode::TEXT;
		Options.m_Clearable = Key == "palette_search";
		Options.m_FontSize = 12.0f;
		if(ui_widget::InputField(Context, &Field, Rect, Options).m_Changed) { Value = Field.GetString(); return true; } return false;
	};
	auto Dropdown = [&](const std::string &Key, int Current, const char *const *pNames, int Count, CUIRect Rect) {
		Rect.Margin(1, &Rect);
		if(E.m_Drag.Active()) { Surface(Rect, Theme.m_InputSurface); if(Current >= 0 && Current < Count) Ui()->DoLabel(&Rect, pNames[Current], 12, TEXTALIGN_ML); return Current; }
		auto &State = E.Widget(Key).m_Drop;
		std::vector<const char *> Names(pNames, pNames + Count); return Ui()->DoDropDown(&Rect, Current, Names.data(), Count, State);
	};
	auto BarButton = [&](CUIRect &Bar, const char *pKey, const char *pText, float Width) {
		CUIRect Rect; Bar.VSplitLeft(std::min(Width, Bar.w), &Rect, &Bar); Bar.VSplitLeft(3, nullptr, &Bar); return Button(pKey, pText, Rect);
	};
	auto SelectedIndex = [&]() { for(size_t i = 0; i < System.m_vMods.size(); ++i) if(System.m_vMods[i].m_Id == E.m_Mod) return static_cast<int>(i); return -1; };
	if(BarButton(Toolbar, "new", "新建", 48))
	{
		std::string Name = "新模组"; int N = 2;
		while(std::any_of(System.m_vMods.begin(), System.m_vMods.end(), [&](const ms::CMod &M) { return M.m_Name == Name; })) Name = "新模组" + std::to_string(N++);
		int Index = System.AddMod(Name); E.Select(System.m_vMods[Index].m_Id);
	}
	if(BarButton(Toolbar, "directory", "打开目录", 74)) System.OpenDirectory();
	if(BarButton(Toolbar, "transfer", E.m_Transfer ? "收起导入" : "导入/导出", 74)) E.m_Transfer = !E.m_Transfer;
	if(SelectedIndex() < 0 && !System.m_vMods.empty()) E.Select(System.m_vMods[0].m_Id);
	int Selected = SelectedIndex();
	if(Selected >= 0)
	{
		if(BarButton(Toolbar, "save", "保存", 48)) { if(System.Save(Selected, E.m_Status)) E.m_Status = "已保存"; }
		if(BarButton(Toolbar, "reload", E.m_Reload == E.m_Mod ? "确认重载?" : "重载", 74))
		{
			if(System.Reload(Selected, E.m_Reload == E.m_Mod, E.m_Status)) { E.m_Reload = 0; E.m_Rule = 0; E.m_Drag.Cancel(); }
			else if(System.m_vMods[Selected].m_Dirty) E.m_Reload = E.m_Mod;
		}
		if(BarButton(Toolbar, "delete", E.m_Delete == E.m_Mod ? "确认删除?" : "删除", 74))
		{
			if(E.m_Delete == E.m_Mod) { if(System.DeleteMod(Selected, E.m_Status)) E.Select(0); return; }
			E.m_Delete = E.m_Mod; E.m_Status = "再次点击删除，将同时删除磁盘 .mod 文件";
		}
		if(BarButton(Toolbar, "page", E.m_Editing ? "← 返回" : "编辑 →", 68)) { E.m_Editing = !E.m_Editing; E.m_Drag.Cancel(); }
		if(BarButton(Toolbar, "help", "? 帮助", 60)) E.m_Help = "动作读取数据或改变游戏；如果支持且/或/非；循环和等待支持表达式。按住拖放或点击拿起再点击放下。";
	}
	if(Selected >= 0)
	{
		auto &Mod = System.m_vMods[Selected];
		if(E.m_Seen != Mod.m_Id) { E.m_Seen = Mod.m_Id; E.m_Rename = Mod.m_Name; }
		CUIRect Enabled, Name, Rename, Switch; Metadata.VSplitLeft(62, &Enabled, &Metadata);
		if(!E.m_Drag.Active() && DoButton_CheckBox(&E.Widget("enabled").m_Button, "启用", Mod.m_Enabled, &Enabled)) System.SetEnabled(Selected, !Mod.m_Enabled);
		Metadata.VSplitRight(std::min(150.0f, Metadata.w * 0.3f), &Metadata, &Switch); Metadata.VSplitRight(52, &Name, &Rename);
		Edit("rename/" + std::to_string(Mod.m_Id), E.m_Rename, Name);
		if(Button("rename_apply", "改名", Rename)) { if(System.Rename(Selected, E.m_Rename, E.m_Status)) E.m_Status = "名称与文件已迁移"; }
		std::vector<const char *> Names; for(const auto &M : System.m_vMods) Names.push_back(M.m_Name.c_str());
		int New = Dropdown("mod_switch", Selected, Names.data(), static_cast<int>(Names.size()), Switch);
		if(New != Selected && New >= 0 && New < static_cast<int>(System.m_vMods.size())) { E.Select(System.m_vMods[New].m_Id); Selected = New; }
	}
	else Ui()->DoLabel(&Metadata, "新建模组或输入 .mod 文件路径进行导入", 12, TEXTALIGN_ML);
	if(E.m_Transfer)
	{
		MainView.HSplitTop(28, &Transfer, &MainView);
		CUIRect Path, Import, Export; Transfer.VSplitRight(55, &Transfer, &Export); Transfer.VSplitRight(55, &Path, &Import);
		Edit("transfer_path", E.m_Path, Path); E.Widget("transfer_path").m_Input.SetEmptyText("导入 / 导出路径，例如 C:/mods/example.mod");
		if(Button("import", "导入", Import)) { int Index = System.Import(E.m_Path, E.m_Status); if(Index >= 0) { E.Select(System.m_vMods[Index].m_Id); Selected = Index; E.m_Status = "已导入；保存后进入 mods 目录"; } }
		if(Button("export", "导出", Export) && Selected >= 0) { if(System.Export(Selected, E.m_Path, E.m_Status)) E.m_Status = "已导出"; }
	}
	if(!E.m_Editing)
	{
		CUIRect Row; MainView.HSplitTop(22, &Row, &MainView); Ui()->DoLabel(&Row, "从示例开始 · 创建后可以修改所有积木", 12, TEXTALIGN_ML);
		MainView.HSplitTop(30, &Row, &MainView);
		const char *apExamples[] = {"按键提示 · G", "出生延迟提醒"};
		for(int i = 0; i < 2; ++i)
		{
			CUIRect Example; Row.VSplitLeft(Row.w / (2 - i), &Example, &Row); Example.VMargin(2, &Example);
			if(Button("example/" + std::to_string(i), apExamples[i], Example))
			{
				Pending = [&, i, ExampleName = std::string(apExamples[i])] {
					std::string Name = ExampleName; int Suffix = 2;
					while(std::any_of(System.m_vMods.begin(), System.m_vMods.end(), [&](const ms::CMod &M) { return M.m_Name == Name; })) Name = ExampleName + " " + std::to_string(Suffix++);
					const int Index = System.AddMod(Name); auto &Mod = System.m_vMods[Index];
					Mod.m_vRules = {ExampleRule(i)}; Mod.m_Enabled = false; System.Changed(Mod);
					E.Select(Mod.m_Id, Mod.m_vRules[0].m_Id); E.m_Editing = true;
					E.m_Status = "示例已创建；修改后保存，勾选启用即可运行";
				};
			}
		}
		MainView.HSplitTop(8, nullptr, &MainView);
		MainView.HSplitTop(24, &Row, &MainView);
		CUIRect Name, Enable, Count, Status, EditRect, Delete;
		Row.VSplitLeft(Row.w * 0.34f, &Name, &Row); Row.VSplitLeft(44, &Enable, &Row); Row.VSplitLeft(44, &Count, &Row);
		Row.VSplitRight(65, &Row, &Delete); Row.VSplitRight(55, &Status, &EditRect);
		Ui()->DoLabel(&Name, "模组名称", 12, TEXTALIGN_ML); Ui()->DoLabel(&Enable, "启用", 12, TEXTALIGN_MC);
		Ui()->DoLabel(&Count, "规则", 12, TEXTALIGN_MC); Ui()->DoLabel(&Status, "保存状态", 12, TEXTALIGN_ML);
		Card(MainView);
		auto Params = ScrollParams(Theme); E.m_Manager.Begin(&MainView, &Params);
		for(size_t i = 0; i < System.m_vMods.size(); ++i)
		{
			auto &M = System.m_vMods[i]; MainView.HSplitTop(38, &Row, &MainView); if(!E.m_Manager.AddRect(Row)) continue;
			Row.HMargin(2, &Row);
			Surface(Row, Theme.m_TextBody.WithAlpha(M.m_Id == E.m_Mod ? 0.10f : 0.025f));
			CUIRect Name, Enable, Count, Status, EditRect, Delete;
			Row.VSplitLeft(Row.w * 0.34f, &Name, &Row); Row.VSplitLeft(44, &Enable, &Row); Row.VSplitLeft(44, &Count, &Row);
			Row.VSplitRight(65, &Row, &Delete); Row.VSplitRight(55, &Status, &EditRect);
			std::string Key = std::to_string(M.m_Id);
			if(Button(Key + "/select", M.m_Name.c_str(), Name, M.m_Id == E.m_Mod)) E.Select(M.m_Id);
			if(DoButton_CheckBox(&E.Widget(Key + "/enabled").m_Button, "", M.m_Enabled, &Enable)) System.SetEnabled(static_cast<int>(i), !M.m_Enabled);
			Ui()->DoLabel(&Count, std::to_string(M.m_vRules.size()).c_str(), 12, TEXTALIGN_MC); Ui()->DoLabel(&Status, M.m_RemovedAimActions ? "待检查" : M.m_Dirty ? "未保存" : "已保存", 12, TEXTALIGN_ML);
			if(Button(Key + "/edit", "编辑", EditRect)) { E.Select(M.m_Id); E.m_Editing = true; }
			if(Button(Key + "/delete", E.m_Delete == M.m_Id ? "确认?" : "删除", Delete))
			{
				if(E.m_Delete == M.m_Id) Pending = [&, Index = static_cast<int>(i)] { if(System.DeleteMod(Index, E.m_Status)) E.Select(0); };
				else { E.m_Delete = M.m_Id; E.m_Status = "再次点击确认，将删除文件与整个模组"; }
			}
		}
		E.m_Manager.End();
	}
	else if(Selected >= 0)
	{
		auto &Mod = System.m_vMods[Selected];
		if(Mod.m_vRules.empty()) { Mod.m_vRules.emplace_back(); System.Changed(Mod); }
		auto *pRule = ms::FindRule(Mod, E.m_Rule);
		if(!pRule) { E.m_Rule = Mod.m_vRules.front().m_Id; pRule = &Mod.m_vRules.front(); E.m_Drag.Cancel(); }
		if(E.m_Drag.Active() && (E.m_Drag.m_Mod != Mod.m_Id || E.m_Drag.m_Rule != E.m_Rule)) E.m_Drag.Cancel();
		const std::string ModKey = std::to_string(Mod.m_Id);
		auto Changed = [&]() { System.Changed(Mod); };
		auto Pick = [&](CUIRect Rect, int Source, const std::string &Label, const std::string &Text, const ms::CScriptNode &Node, ms::TId Id = 0) {
			if(E.m_Drag.Active() || Ui()->IsPopupOpen() || !Ui()->MouseHovered(&Rect) || !Ui()->MouseButtonClicked(0)) return;
			if(CLineInput::GetActiveInput()) CLineInput::GetActiveInput()->Deactivate();
			auto &D = E.m_Drag; D.m_Source = Source; D.m_Label = Label; D.m_Text = Text; D.m_Node = Node; D.m_Id = Id;
			D.m_Mod = Mod.m_Id; D.m_Rule = E.m_Rule; D.m_Holding = Ui()->MouseButton(0); D.m_Picked = true;
		};
		auto ParamZone = [&](CUIRect Rect, std::string &Field, size_t Begin, size_t End, ms::EParamKind Kind) {
			if(!E.m_Drag.Active() || E.m_Drag.Block()) return;
			if(Kind == ms::PARAM_VAR && (E.m_Drag.m_Source == 4 || E.m_Drag.m_Text == "%v")) return;
			if(Kind == ms::PARAM_STR && E.m_Drag.m_Source == 4) return;
			SZone Z; Z.m_Rect = Z.m_Indicator = Rect; Z.m_pField = &Field; Z.m_Begin = Begin; Z.m_End = End; Z.m_Kind = Kind;
			Z.m_Hover = !Ui()->IsPopupOpen() && Ui()->MouseHovered(&Rect); Zones.push_back(Z);
		};
		std::function<bool(const std::string &, std::string &, size_t, size_t, CUIRect, int)> Expression;
		Expression = [&](const std::string &Key, std::string &Field, size_t Begin, size_t End, CUIRect Rect, int Depth) {
			std::string Part = Field.substr(Begin, End - Begin); size_t B, Op, Last;
			if(Depth < 8 && Rect.w > 115 && ms::SplitExpression(Part, B, Op, Last))
			{
				CUIRect Left, Operator, Right; Rect.VSplitLeft((Rect.w - 18) * 0.5f, &Left, &Right); Right.VSplitLeft(18, &Operator, &Right);
				char aOp[2] = {Part[Op], 0}; Ui()->DoLabel(&Operator, aOp, 14, TEXTALIGN_MC);
				if(Expression(Key + "/L", Field, Begin + B, Begin + Op, Left, Depth + 1)) return true;
				return Expression(Key + "/R", Field, Begin + Op + 1, Begin + Last, Right, Depth + 1);
			}
			ParamZone(Rect, Field, Begin, End, ms::PARAM_INT);
			if(Edit(Key, Part, Rect)) { Field.replace(Begin, End - Begin, Part); return true; } return false;
		};
		auto Parameter = [&](const std::string &Key, const char *pLabel, std::string &Field, CUIRect Rect, ms::EParamKind Kind) {
			CUIRect Label; Rect.VSplitLeft(std::min(Rect.w * 0.28f, 54.0f), &Label, &Rect); Ui()->DoLabel(&Label, pLabel, 12, TEXTALIGN_ML);
			bool Result;
			if(Kind == ms::PARAM_INT) Result = Expression(Key, Field, 0, Field.size(), Rect, 0);
			else { ParamZone(Rect, Field, 0, Field.size(), Kind); Result = Edit(Key, Field, Rect); }
			if(Result) Changed();
		};
		CUIRect Left, Canvas;
		float LeftWidth = std::min(620.0f, std::max(std::min(300.0f, MainView.w * 0.48f), MainView.w * 0.42f));
		MainView.VSplitLeft(LeftWidth, &Left, &Canvas); Canvas.VSplitLeft(8, nullptr, &Canvas);
		CUIRect Library, Values; Left.HSplitBottom(std::min(176.0f, Left.h * 0.48f), &Library, &Values);
		Library.HSplitBottom(6, &Library, nullptr);
		Card(Library); Card(Values); Card(Canvas);
		CUIRect Search; Library.HSplitTop(26, &Search, &Library);
		E.Widget("palette_search").m_Input.SetEmptyText("搜索积木或用途…");
		if(Edit("palette_search", E.m_Search, Search)) E.m_Palette.Reset();
		auto Params = ScrollParams(Theme); E.m_Palette.Begin(&Library, &Params);
		CUIRect Row; Library.HSplitTop(24, &Row, &Library); E.m_Palette.AddRect(Row); Ui()->DoLabel(&Row, "动作积木 · 按住拖或点击拿起", 14, TEXTALIGN_ML);
		int Matches = 0;
		for(int i = 0; i < ms::NUM_ACTIONS + 3; ++i)
		{
			ms::CScriptNode N; std::string Label, Help;
			if(i < ms::NUM_ACTIONS) { N.m_Block.m_Action = static_cast<ms::EAction>(i); N.m_Block.m_Argument = ms::ActionParamKind(N.m_Block.m_Action) == ms::PARAM_INT ? "0" : ""; Label = ms::ACTION_NAMES[i]; Help = ms::ACTION_HELP[i]; }
			else { N.m_Kind = static_cast<ms::CScriptNode::EKind>(i - ms::NUM_ACTIONS + 1); Label = i == ms::NUM_ACTIONS ? "如果 / 否则" : i == ms::NUM_ACTIONS + 1 ? "重复循环" : "等待 tick"; Help = i == ms::NUM_ACTIONS ? "按且 / 或 / 非组合条件，拖子块进入分支。" : "次数支持数值、变量、%v 或表达式。"; }
			if(!E.m_Search.empty() && !str_utf8_find_nocase(Label.c_str(), E.m_Search.c_str()) && !str_utf8_find_nocase(Help.c_str(), E.m_Search.c_str())) continue;
			++Matches;
			Library.HSplitTop(52, &Row, &Library);
			if(!E.m_Palette.AddRect(Row)) continue;
			Row.HMargin(2, &Row);
			DrawBlock(Row, N.m_Kind, "palette/" + std::to_string(i));
			CUIRect Title, Description; Row.Margin(8, &Row); Row.HSplitTop(18, &Title, &Description);
			Ui()->DoLabel(&Title, Label.c_str(), 14, TEXTALIGN_ML); Ui()->DoLabel(&Description, Help.c_str(), 10, TEXTALIGN_ML);
			if(Ui()->MouseHovered(&Row)) E.m_Help = Help;
			Pick(Row, 1, Label, {}, N);
		}
		if(Matches == 0) { Library.HSplitTop(28, &Row, &Library); if(E.m_Palette.AddRect(Row)) Ui()->DoLabel(&Row, "没有匹配的积木，试试“角度”或“变量”", 12, TEXTALIGN_ML); }
		E.m_Palette.End();
		CUIRect Tabs, Tab; Values.HSplitTop(24, &Tabs, &Values);
		Tabs.VSplitLeft(Tabs.w * 0.5f, &Tab, &Tabs);
		if(Button("vars_tab", "变量与寄存器", Tab, !E.m_Parameters)) E.m_Parameters = false;
		if(Button("params_tab", "模组参数", Tabs, E.m_Parameters)) E.m_Parameters = true;
		E.m_Values.Begin(&Values, &Params);
		Values.HSplitTop(24, &Row, &Values); if(E.m_Values.AddRect(Row))
		{
			CUIRect Register, Ops; Row.VSplitLeft(Row.w * 0.38f, &Register, &Ops);
			Surface(Register, Theme.m_TextBody.WithAlpha(0.08f)); Ui()->DoLabel(&Register, "%v 寄存器", 12, TEXTALIGN_MC);
			Pick(Register, 3, "%v", "%v", {});
			for(char Op : std::string("+-*/%")) { CUIRect Chip; Ops.VSplitLeft(Ops.w / (6 - static_cast<int>(std::string("+-*/%").find(Op))), &Chip, &Ops); std::string Text(1, Op); Chip.Margin(1, &Chip); Surface(Chip, Theme.m_TextBody.WithAlpha(0.04f)); Ui()->DoLabel(&Chip, Text.c_str(), 14, TEXTALIGN_MC); Pick(Chip, 4, Text, Text, {}); }
		}
		Values.HSplitTop(26, &Row, &Values); if(E.m_Values.AddRect(Row))
		{
			CUIRect Name, Add; Row.VSplitRight(55, &Name, &Add); Edit("new_value_name", E.m_NewName, Name);
			E.Widget("new_value_name").m_Input.SetEmptyText("名称留空自动命名");
			if(Button("new_value", "新建", Add))
			{
				Pending = [&] {
					std::string Name = E.m_NewName; int N = 1;
					auto Taken = [&](const std::string &Text) { for(const auto &V : Mod.m_vVars) if(V.m_Name == Text) return true; for(const auto &P : Mod.m_vParams) if(P.m_Name == Text) return true; return false; };
					if(Name.empty()) { do { Name = (E.m_Parameters ? "参数" : "变量") + std::to_string(N++); } while(Taken(Name)); }
					if(Taken(Name)) { E.m_Status = "变量或参数名称重复"; return; }
					if(E.m_Parameters) { if(Mod.m_vParams.size() >= 256) return; ms::CModParam P; P.m_Name = Name; Mod.m_vParams.push_back(P); }
					else { if(Mod.m_vVars.size() >= 256) return; ms::CModVar V; V.m_Name = Name; Mod.m_vVars.push_back(V); }
					E.m_NewName.clear(); Changed();
				};
			}
		}
		if(!E.m_Parameters)
		{
			for(auto &V : Mod.m_vVars)
			{
				Values.HSplitTop(26, &Row, &Values); if(!E.m_Values.AddRect(Row)) continue;
				CUIRect Chip, Field, Delete; Row.VSplitLeft(Row.w * 0.38f, &Chip, &Row); Row.VSplitRight(24, &Field, &Delete);
				Surface(Chip, Theme.m_TextBody.WithAlpha(0.06f)); Ui()->DoLabel(&Chip, V.m_Name.c_str(), 12, TEXTALIGN_MC);
				Pick(Chip, 3, V.m_Name, V.m_Name, {});
				Parameter(ModKey + "/" + std::to_string(V.m_Id), "初值", V.m_Initial, Field, ms::PARAM_INT);
				if(Button(std::to_string(V.m_Id) + "/delete", "x", Delete)) Pending = [&, Id = V.m_Id] { Mod.m_vVars.erase(std::remove_if(Mod.m_vVars.begin(), Mod.m_vVars.end(), [Id](const ms::CModVar &X) { return X.m_Id == Id; }), Mod.m_vVars.end()); Changed(); };
				if(Ui()->MouseHovered(&Chip)) { auto *pState = System.State(Mod.m_Id); double Value = 0; if(pState) { auto It = pState->m_Variables.find(V.m_Name); if(It != pState->m_Variables.end()) Value = It->second; } E.m_Help = V.m_Name + " · 当前值 " + ms::FormatValue(Value) + "；输入框编辑初值，跨 tick 保存当前值。"; }
			}
		}
		else for(auto &P : Mod.m_vParams)
		{
			Values.HSplitTop(48, &Row, &Values); if(!E.m_Values.AddRect(Row)) continue;
			CUIRect Name, Type, Delete, Field; Row.HSplitTop(22, &Name, &Field); Name.VSplitRight(24, &Name, &Delete); Name.VSplitRight(62, &Name, &Type);
			if(Edit(std::to_string(P.m_Id) + "/name", P.m_Name, Name)) Changed();
			if(Button(std::to_string(P.m_Id) + "/type", P.m_Numeric ? "数值" : "文本", Type)) { P.m_Numeric = !P.m_Numeric; Changed(); }
			if(Button(std::to_string(P.m_Id) + "/delete", "x", Delete)) Pending = [&, Id = P.m_Id] { Mod.m_vParams.erase(std::remove_if(Mod.m_vParams.begin(), Mod.m_vParams.end(), [Id](const ms::CModParam &X) { return X.m_Id == Id; }), Mod.m_vParams.end()); Changed(); };
			Parameter(std::to_string(P.m_Id) + "/value", "参数值", P.m_Value, Field, P.m_Numeric ? ms::PARAM_INT : ms::PARAM_STR);
			if(Ui()->MouseHovered(&Name)) E.m_Help = "数值参数作为表达式中的名称使用；文本参数用 %参数名%。参数只读，存入变量不会修改参数。";
		}
		E.m_Values.End();
		CUIRect RuleBar, EventBar;
		Canvas.HSplitTop(26, &RuleBar, &Canvas);
		CUIRect Previous, Next, AddRule, DeleteRule, CopyRule, RuleTitle;
		RuleBar.VSplitLeft(26, &Previous, &RuleBar); RuleBar.VSplitLeft(26, &Next, &RuleBar);
		RuleBar.VSplitRight(48, &RuleBar, &DeleteRule); RuleBar.VSplitRight(48, &RuleBar, &CopyRule); RuleBar.VSplitRight(48, &RuleTitle, &AddRule);
		size_t RuleIndex = 0;
		while(RuleIndex < Mod.m_vRules.size() && Mod.m_vRules[RuleIndex].m_Id != E.m_Rule) ++RuleIndex;
		Ui()->DoLabel(&RuleTitle, ("规则 " + std::to_string(RuleIndex + 1) + " / " + std::to_string(Mod.m_vRules.size())).c_str(), 13, TEXTALIGN_MC);
		auto SelectRule = [&](size_t Index) { E.m_Rule = Mod.m_vRules[Index].m_Id; E.m_Canvas.Reset(); E.m_Drag.Cancel(); };
		if(Button("rule_previous", "<", Previous)) SelectRule((RuleIndex + Mod.m_vRules.size() - 1) % Mod.m_vRules.size());
		if(Button("rule_next", ">", Next)) SelectRule((RuleIndex + 1) % Mod.m_vRules.size());
		if(Button("rule_add", "+ 规则", AddRule)) Pending = [&] { if(Mod.m_vRules.size() >= ms::MAX_RULES) { E.m_Status = "规则数量已达 64 条上限"; return; } Mod.m_vRules.emplace_back(); E.m_Rule = Mod.m_vRules.back().m_Id; E.m_Canvas.Reset(); Changed(); };
		if(Button("rule_copy", "复制", CopyRule)) Pending = [&, Id = E.m_Rule] {
			ms::TId NewRule = 0;
			if(ms::DuplicateRule(Mod, Id, NewRule, E.m_Status)) { E.m_Rule = NewRule; E.m_Canvas.Reset(); Changed(); E.m_Status = "规则已复制，可独立修改事件和积木"; }
		};
		if(Button("rule_delete", "- 规则", DeleteRule)) Pending = [&, Id = E.m_Rule] { Mod.m_vRules.erase(std::remove_if(Mod.m_vRules.begin(), Mod.m_vRules.end(), [Id](const ms::CRule &R) { return R.m_Id == Id; }), Mod.m_vRules.end()); if(Mod.m_vRules.empty()) Mod.m_vRules.emplace_back(); E.m_Rule = Mod.m_vRules.front().m_Id; E.m_Canvas.Reset(); Changed(); };
		pRule = ms::FindRule(Mod, E.m_Rule);
		Canvas.HSplitTop(26, &EventBar, &Canvas);
		CUIRect EventLabel, EventChoice, Key;
		EventBar.VSplitLeft(38, &EventLabel, &EventBar); EventBar.VSplitLeft(EventBar.w * 0.6f, &EventChoice, &Key);
		Ui()->DoLabel(&EventLabel, "事件", 12, TEXTALIGN_ML);
		const std::string RuleKey = "rule/" + std::to_string(pRule->m_Id);
		int Hook = Dropdown(RuleKey + "/event", pRule->m_Hook, ms::HOOK_NAMES.data(), ms::NUM_HOOKS, EventChoice);
		if(Hook != pRule->m_Hook) { pRule->m_Hook = static_cast<ms::EHook>(Hook); Changed(); }
		if(pRule->m_Hook == ms::HOOK_ON_KEY_PRESS) Parameter(RuleKey + "/key", "按键", pRule->m_Argument, Key, ms::PARAM_STR);
		CUIRect ViewBar, Expand, Collapse, ViewHint; Canvas.HSplitTop(26, &ViewBar, &Canvas);
		ViewBar.VSplitLeft(66, &Expand, &ViewBar); ViewBar.VSplitLeft(66, &Collapse, &ViewHint);
		std::function<void(const std::vector<ms::CScriptNode> &, bool)> ExpandTree;
		ExpandTree = [&](const std::vector<ms::CScriptNode> &Nodes, bool Expanded) {
			for(const auto &Node : Nodes) { if(Node.HasChildren()) E.m_Expanded[Node.m_Id] = Expanded; ExpandTree(Node.m_vThen, Expanded); ExpandTree(Node.m_vElse, Expanded); }
		};
		if(Button("expand_all", "展开全部", Expand)) ExpandTree(pRule->m_vBody, true);
		if(Button("collapse_all", "折叠全部", Collapse)) ExpandTree(pRule->m_vBody, false);
		Ui()->DoLabel(&ViewHint, ":: 移动 · 复制保留子块", 10, TEXTALIGN_MR);
		const CUIRect CanvasClip = Canvas;
		E.m_Canvas.Begin(&Canvas, &Params);
		auto Line = [&](float Height, int Depth, CUIRect &Rect) {
			Canvas.HSplitTop(Height, &Rect, &Canvas);
			Rect.VSplitLeft(Depth * 12.0f + 4, nullptr, &Rect); Rect.VSplitRight(4, &Rect, nullptr);
			return E.m_Canvas.AddRect(Rect);
		};
		auto BlockZone = [&](CUIRect Rect, CUIRect Indicator, ms::SSequenceTarget Target, size_t Index) {
			if(!E.m_Drag.Block() || E.m_Canvas.RectClipped(Rect)) return;
			SZone Z; Z.m_Rect = Rect; Z.m_Indicator = Indicator; Z.m_Target = Target; Z.m_Index = Index;
			Z.m_Hover = !Ui()->IsPopupOpen() && Ui()->MouseHovered(&Rect); Zones.push_back(Z);
			if(Z.m_Hover) Indicator.Draw(ColorRGBA(0.24f, 0.85f, 0.55f, 1), 0, 0);
		};
		auto Conditions = [&](std::vector<ms::CCondition> &Conditions, const std::string &Key, int Depth) {
			CUIRect Title, Add;
			if(Line(26, Depth, Title))
			{
				Surface(Title, Theme.m_TextBody.WithAlpha(0.04f)); Title.VSplitRight(62, &Title, &Add);
				Ui()->DoLabel(&Title, "判定条件 · 且 / 或 / 非", 12, TEXTALIGN_ML);
				if(Button(Key + "/add_condition", "+ 条件", Add)) Pending = [&, pConditions = &Conditions] { if(pConditions->size() < 64) { pConditions->emplace_back(); Changed(); } };
			}
			for(size_t i = 0; i < Conditions.size(); ++i)
			{
				auto &C = Conditions[i]; const std::string CK = Key + "/condition/" + std::to_string(C.m_Id);
				CUIRect Row, Logic, Negate, Choice, Delete;
				if(Line(26, Depth, Row))
				{
					Surface(Row, Theme.m_TextBody.WithAlpha(0.025f));
					Row.VSplitLeft(32, &Logic, &Row); Row.VSplitLeft(35, &Negate, &Row); Row.VSplitRight(24, &Choice, &Delete);
					if(i == 0) Ui()->DoLabel(&Logic, "若", 12, TEXTALIGN_MC);
					else { const char *apLogic[] = {"且", "或"}; int L = Dropdown(CK + "/logic", C.m_Logic, apLogic, 2, Logic); if(L != C.m_Logic) { C.m_Logic = static_cast<ms::ELogic>(L); Changed(); } }
					if(Button(CK + "/not", C.m_Not ? "非 ✓" : "非", Negate)) { C.m_Not = !C.m_Not; Changed(); }
					int Type = Dropdown(CK + "/type", C.m_Type, ms::CONDITION_NAMES.data(), ms::NUM_CONDITIONS, Choice);
					if(Type != C.m_Type) { C.m_Type = static_cast<ms::ECondition>(Type); Changed(); }
					if(Button(CK + "/delete", "x", Delete)) Pending = [&, pConditions = &Conditions, Id = C.m_Id] { pConditions->erase(std::remove_if(pConditions->begin(), pConditions->end(), [Id](const ms::CCondition &X) { return X.m_Id == Id; }), pConditions->end()); Changed(); };
				}
				if(ms::ConditionUsesVarAndValue(C.m_Type))
				{
					if(Line(26, Depth + 1, Row)) { CUIRect Left, Right; Row.VSplitMid(&Left, &Right, 4); Parameter(CK + "/left", "变量", C.m_Left, Left, ms::PARAM_VAR); Parameter(CK + "/right", "比较值", C.m_Right, Right, ms::PARAM_INT); }
				}
				else if(C.m_Type == ms::COND_RANDOM_PROBABILITY && Line(26, Depth + 1, Row)) Parameter(CK + "/probability", "概率 %", C.m_Right, Row, ms::PARAM_INT);
			}
		};
		Conditions(pRule->m_vConditions, RuleKey, 0);
		std::function<void(std::vector<ms::CScriptNode> &, ms::SSequenceTarget, int, const char *)> Sequence;
		Sequence = [&](std::vector<ms::CScriptNode> &Nodes, ms::SSequenceTarget Target, int Depth, const char *pEndLabel) {
			for(size_t i = 0; i < Nodes.size(); ++i)
			{
				auto &N = Nodes[i]; const std::string NK = "node/" + std::to_string(N.m_Id);
				bool &Expanded = E.m_Expanded.try_emplace(N.m_Id, true).first->second;
				std::string Label = N.m_Kind == ms::CScriptNode::K_ACTION ? ms::ACTION_NAMES[N.m_Block.m_Action] : N.m_Kind == ms::CScriptNode::K_IF_ELSE ? "如果" : N.m_Kind == ms::CScriptNode::K_LOOP ? "重复循环" : "等待 tick";
				CUIRect Row, Whole;
				if(Line(30, Depth, Row))
				{
					Whole = Row; DrawBlock(Row, N.m_Kind, NK + "/surface");
					CUIRect Grab, Fold, Title, Else, Duplicate, Delete;
					Row.VSplitLeft(20, &Grab, &Row); Row.VSplitLeft(20, &Fold, &Row); Row.VSplitRight(24, &Row, &Delete); Row.VSplitRight(36, &Row, &Duplicate);
					if(N.m_Kind == ms::CScriptNode::K_IF_ELSE && !N.m_HasElse) Row.VSplitRight(58, &Title, &Else); else Title = Row;
					Ui()->DoLabel(&Grab, "::", 14, TEXTALIGN_MC); Pick(Grab, 2, Label, {}, {}, N.m_Id);
					if(N.HasChildren() && Button(NK + "/fold", Expanded ? "v" : ">", Fold)) Expanded = !Expanded;
					Ui()->DoLabel(&Title, Label.c_str(), 13, TEXTALIGN_ML);
					if(N.m_Kind == ms::CScriptNode::K_IF_ELSE && !N.m_HasElse && Button(NK + "/add_else", "+ 否则", Else)) { N.m_HasElse = true; Changed(); }
					if(Button(NK + "/duplicate", "复制", Duplicate)) Pending = [&, Id = N.m_Id] { if(ms::DuplicateNode(Mod, Id, E.m_Status)) { Changed(); E.m_Status = "积木与子块已复制到原位置之后"; } };
					if(Button(NK + "/delete", "x", Delete)) Pending = [&, Id = N.m_Id] { ms::DeleteNode(Mod, Id); Changed(); };
					if(Ui()->MouseHovered(&Whole)) E.m_Help = N.m_Kind == ms::CScriptNode::K_ACTION ? ms::ACTION_HELP[N.m_Block.m_Action] : "抓住 :: 可搬动整棵积木；标题上、下半分别插到前面、后面。容器末尾可接收新积木。";
					CUIRect Top, Bottom, Indicator; Whole.HSplitMid(&Top, &Bottom); Indicator = Whole; Indicator.h = 3;
					BlockZone(Top, Indicator, Target, i); Indicator.y = Whole.y + Whole.h - 3; BlockZone(Bottom, Indicator, Target, i + 1);
				}
				if(N.m_Kind == ms::CScriptNode::K_ACTION)
				{
					const auto Kind = ms::ActionParamKind(N.m_Block.m_Action);
					if(Kind != ms::PARAM_NONE && Line(26, Depth, Row)) Parameter(NK + "/argument", Kind == ms::PARAM_STR ? "文字" : Kind == ms::PARAM_VAR ? "变量名" : "数值", N.m_Block.m_Argument, Row, Kind);
					if(ms::ActionUsesGetValue(N.m_Block.m_Action) && Line(26, Depth, Row))
					{
						CUIRect LabelRect; Row.VSplitLeft(42, &LabelRect, &Row); Ui()->DoLabel(&LabelRect, "取值", 12, TEXTALIGN_ML);
						int Value = Dropdown(NK + "/value", N.m_Block.m_Value, ms::VALUE_NAMES.data(), ms::NUM_GETVALUES, Row); if(Value != N.m_Block.m_Value) { N.m_Block.m_Value = static_cast<ms::EGetValue>(Value); Changed(); }
					}
				}
				else if(N.m_Kind == ms::CScriptNode::K_LOOP || N.m_Kind == ms::CScriptNode::K_WAIT)
				{
					if(Line(26, Depth, Row)) Parameter(NK + "/count", N.m_Kind == ms::CScriptNode::K_LOOP ? "次数" : "等待", N.m_Count, Row, ms::PARAM_INT);
				}
				if(N.HasChildren())
				{
					if(Expanded)
					{
						if(N.m_Kind == ms::CScriptNode::K_IF_ELSE) Conditions(N.m_vIfConds, NK, Depth + 1);
						Sequence(N.m_vThen, {Target.m_Rule, N.m_Id, false}, Depth + 1, N.m_Kind == ms::CScriptNode::K_IF_ELSE ? "放进如果分支" : "放进循环");
					}
					else if(Line(24, Depth + 1, Row))
					{
						Surface(Row, Theme.m_TextBody.WithAlpha(0.025f)); Ui()->DoLabel(&Row, ("已折叠 " + std::to_string(N.m_vThen.size()) + " 块 · 可放进容器").c_str(), 12, TEXTALIGN_MC);
						BlockZone(Row, Row, {Target.m_Rule, N.m_Id, false}, N.m_vThen.size());
					}
					if(N.m_Kind == ms::CScriptNode::K_IF_ELSE && N.m_HasElse)
					{
						if(Line(26, Depth + 1, Row))
						{
							Surface(Row, Theme.m_TextBody.WithAlpha(0.04f)); CUIRect Title, Delete; Row.VSplitRight(62, &Title, &Delete); Ui()->DoLabel(&Title, "否则", 13, TEXTALIGN_ML);
							if(Button(NK + "/remove_else", "- 否则", Delete)) Pending = [&, Id = N.m_Id] { auto *pNode = ms::FindNode(pRule->m_vBody, Id); if(pNode) { pNode->m_vElse.clear(); pNode->m_HasElse = false; Changed(); } };
							BlockZone(Title, Title, {Target.m_Rule, N.m_Id, true}, 0);
						}
						if(Expanded) Sequence(N.m_vElse, {Target.m_Rule, N.m_Id, true}, Depth + 1, "放进否则分支");
					}
				}
			}
			CUIRect End;
			if(Line(24, Depth, End))
			{
				Surface(End, Theme.m_TextBody.WithAlpha(0.025f));
				Ui()->DoLabel(&End, Nodes.empty() ? "（空）拖一块进来" : pEndLabel, 12, TEXTALIGN_MC);
				BlockZone(End, End, Target, Nodes.size());
			}
		};
		Sequence(pRule->m_vBody, {pRule->m_Id, 0, false}, 0, "放在规则末尾");
		if(E.m_Drag.Active() && Ui()->MouseHovered(&CanvasClip)) E.m_Canvas.DoEdgeScrolling();
		E.m_Canvas.End();
		if(E.m_Drag.Active())
		{
			SZone *pHover = nullptr;
			for(auto &Z : Zones) if(Z.m_Hover) pHover = &Z;
			bool Place = !E.m_Drag.m_Picked && (E.m_Drag.m_Holding ? !Ui()->MouseButton(0) : Ui()->MouseButtonClicked(0));
			if(E.m_Drag.m_Holding && !Ui()->MouseButton(0)) E.m_Drag.m_Holding = false;
			if(Place && pHover)
			{
				if(E.m_Drag.Block())
				{
					bool Success = E.m_Drag.m_Source == 1 ? ms::InsertNode(Mod, E.m_Drag.m_Node, pHover->m_Target, pHover->m_Index, E.m_Status) : ms::MoveNode(Mod, E.m_Drag.m_Id, pHover->m_Target, pHover->m_Index, E.m_Status);
					if(Success) { Changed(); E.m_Status = "积木已放置"; E.m_Drag.Cancel(); }
				}
				else if(pHover->m_pField)
				{
					auto &Field = *pHover->m_pField; std::string Value = E.m_Drag.m_Text;
					if(E.m_Drag.m_Source == 4) { std::string Old = Field.substr(pHover->m_Begin, pHover->m_End - pHover->m_Begin); Value = "(" + (Old.empty() ? "0" : Old) + " " + Value + " 0)"; }
					if(pHover->m_Kind == ms::PARAM_STR) Field += Value == "%v" ? "%v" : "%" + Value + "%";
					else Field.replace(pHover->m_Begin, pHover->m_End - pHover->m_Begin, Value);
					Changed(); E.m_Status = "参数已填入"; E.m_Drag.Cancel();
				}
			}
			if(E.m_Drag.Active())
			{
				CUIRect Ghost = {Ui()->MouseX() + 12, Ui()->MouseY() + 12, 180, 46};
				Surface(Ghost, CompositeUiSurface(Theme.m_Surface), ui_token::radius::BASE);
				CUIRect Label, Hint; Ghost.HSplitTop(24, &Label, &Hint); Ui()->DoLabel(&Label, E.m_Drag.m_Label.c_str(), 13, TEXTALIGN_MC);
				Ui()->DoLabel(&Hint, E.m_Drag.m_Holding ? "松开放置 · 右键取消" : "点击放置 · 右键取消", 11, TEXTALIGN_MC);
			}
		}
		// Deferred mutations execute while the editor lambdas and referenced
		// containers are still alive, after all traversal and drop targets.
		if(Pending) { Pending(); Pending = {}; }
	}
	CUIRect Status, Help; Footer.HSplitTop(20, &Status, &Help);
	std::string HelpText = E.m_Help;
	if(E.m_Editing)
	{
		const auto *pState = System.State(E.m_Mod);
		HelpText += " | %v=" + ms::FormatValue(pState ? pState->m_Register : 0) + (pState && pState->m_Nearest.m_Valid ? " · 已取到目标" : " · 无目标");
	}
	std::string StatusText = E.m_Status.empty() ? System.m_LastError : E.m_Status;
	const int StatusIndex = SelectedIndex();
	if(StatusIndex >= 0 && System.m_vMods[StatusIndex].m_RemovedAimActions)
		StatusText = "旧瞄准动作已移除，此模组已停用；检查剩余积木后保存并手动启用。";
	SLabelProperties FooterProps; FooterProps.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&Status, StatusText.c_str(), 12, TEXTALIGN_ML, FooterProps); Ui()->DoLabel(&Help, HelpText.c_str(), 12, TEXTALIGN_ML, FooterProps);
	if(Pending) Pending();
	if(!E.m_Drag.Active() && !Ui()->IsPopupOpen() && Input()->ModifierIsPressed() && Input()->KeyPress(KEY_S))
	{
		const int Index = SelectedIndex();
		if(Index >= 0 && System.Save(Index, E.m_Status)) E.m_Status = "已保存";
	}
}
