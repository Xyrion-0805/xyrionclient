#include "web_browser.h"

#include <base/str.h>
#include <base/log.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <game/client/gameclient.h>
#include <game/client/QmUi/UiTheme.h>
#include <game/client/QmUi/QmAnimResolve.h>

#include <algorithm>
#include <bit>
#include <functional>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#define IStorage IWindowsStorage
#include <base/windows.h>
#undef NOGDI
#include <windows.h>
#include <wingdi.h>
#include <windowsx.h>
#include <WebView2.h>
#undef IStorage
#include <SDL.h>
#include <SDL_syswm.h>

namespace
{
	template<class T> const GUID &HandlerId();
	template<> const GUID &HandlerId<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>()
	{
		static constexpr GUID Id{0x4e8a3389, 0xc9d8, 0x4bd2, {0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d}}; return Id;
	}
	template<> const GUID &HandlerId<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>()
	{
		static constexpr GUID Id{0x6c4819f3, 0xc9b7, 0x4260, {0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c}}; return Id;
	}
	template<> const GUID &HandlerId<ICoreWebView2AcceleratorKeyPressedEventHandler>()
	{
		static constexpr GUID Id{0xb29c7e28, 0xfa79, 0x41a8, {0x8e, 0x44, 0x65, 0x81, 0x1c, 0x76, 0xdc, 0xb2}}; return Id;
	}
	template<> const GUID &HandlerId<ICoreWebView2NavigationCompletedEventHandler>()
	{
		static constexpr GUID Id{0xd33a35bf, 0x1c49, 0x4f98, {0x93, 0xab, 0x00, 0x6e, 0x05, 0x33, 0xfe, 0x1c}}; return Id;
	}
	template<> const GUID &HandlerId<ICoreWebView2NavigationStartingEventHandler>()
	{
		static constexpr GUID Id{0x9adbe429, 0xf36d, 0x432b, {0x9d, 0xdc, 0xf8, 0x88, 0x1f, 0xbd, 0x76, 0xe3}}; return Id;
	}
	template<> const GUID &HandlerId<ICoreWebView2NewWindowRequestedEventHandler>()
	{
		static constexpr GUID Id{0xd4c185fe, 0xc81c, 0x4989, {0x97, 0xaf, 0x2d, 0x3f, 0xa7, 0xab, 0x56, 0x51}}; return Id;
	}
	template<class T, class... Args>
	class CHandler final : public T
	{
		ULONG m_Refs = 1;
		std::function<HRESULT(Args...)> m_Callback;

	public:
		explicit CHandler(std::function<HRESULT(Args...)> Callback) : m_Callback(std::move(Callback)) {}
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Iid, void **ppObject) override
		{
			if(!ppObject) return E_POINTER;
			*ppObject = nullptr;
			static constexpr GUID UnknownId{0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
			if(!IsEqualIID(Iid, UnknownId) && !IsEqualIID(Iid, HandlerId<T>())) return E_NOINTERFACE;
			*ppObject = static_cast<T *>(this);
			AddRef();
			return S_OK;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++m_Refs; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG Refs = --m_Refs;
			if(!Refs) delete this;
			return Refs;
		}
		HRESULT STDMETHODCALLTYPE Invoke(Args... Values) override { return m_Callback(Values...); }
	};

	constexpr int TITLE_HEIGHT = 42;
	constexpr int TOOLBAR_HEIGHT = 100;
	constexpr int STATUS_HEIGHT = 26;
	COLORREF NativeColor(ColorRGBA Color)
	{
		return RGB(round_to_int(std::clamp(Color.r, 0.0f, 1.0f) * 255), round_to_int(std::clamp(Color.g, 0.0f, 1.0f) * 255), round_to_int(std::clamp(Color.b, 0.0f, 1.0f) * 255));
	}
	COLORREF BlendNative(COLORREF From, COLORREF To, float Amount)
	{
		return RGB(round_to_int(GetRValue(From) + (GetRValue(To) - GetRValue(From)) * Amount),
			round_to_int(GetGValue(From) + (GetGValue(To) - GetGValue(From)) * Amount),
			round_to_int(GetBValue(From) + (GetBValue(To) - GetBValue(From)) * Amount));
	}

	void FillRounded(HDC Dc, const RECT &Rect, COLORREF Color, int Radius, COLORREF Outline)
	{
		const auto Brush = CreateSolidBrush(Color);
		const auto Pen = CreatePen(PS_SOLID, 1, Outline);
		const auto OldBrush = SelectObject(Dc, Brush);
		const auto OldPen = SelectObject(Dc, Pen);
		RoundRect(Dc, Rect.left, Rect.top, Rect.right, Rect.bottom, Radius, Radius);
		SelectObject(Dc, OldPen); SelectObject(Dc, OldBrush);
		DeleteObject(Pen); DeleteObject(Brush);
	}

	void DrawTextLabel(HDC Dc, HFONT Font, COLORREF Color, RECT Rect, const wchar_t *pText, UINT Format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS)
	{
		const auto OldFont = SelectObject(Dc, Font);
		SetBkMode(Dc, TRANSPARENT); SetTextColor(Dc, Color);
		DrawTextW(Dc, pText, -1, &Rect, Format | DT_NOPREFIX);
		SelectObject(Dc, OldFont);
	}

	HWND FindGameWindow()
	{
		SDL_Window *pSdlWindow = SDL_GetKeyboardFocus();
		if(!pSdlWindow) pSdlWindow = SDL_GetMouseFocus();
		SDL_SysWMinfo Info{};
		SDL_VERSION(&Info.version);
		if(pSdlWindow && SDL_GetWindowWMInfo(pSdlWindow, &Info))
			return Info.info.win.window;

		// SDL focus can be absent while opening from the console or after a
		// fullscreen transition. Find this process's SDL host independently.
		HWND Result = nullptr;
		EnumWindows([](HWND Window, LPARAM Data) -> BOOL {
			DWORD ProcessId = 0;
			GetWindowThreadProcessId(Window, &ProcessId);
			wchar_t aClass[64]{};
			if(ProcessId == GetCurrentProcessId() && GetClassNameW(Window, aClass, std::size(aClass)) && wcscmp(aClass, L"SDL_app") == 0)
			{
				*reinterpret_cast<HWND *>(Data) = Window;
				return FALSE;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&Result));
		return Result;
	}
}

class CWebBrowser::CImpl : public std::enable_shared_from_this<CImpl>
{
	ICoreWebView2Controller *m_pController = nullptr;
	ICoreWebView2 *m_pWebView = nullptr;
	HWND m_Window = nullptr, m_Parent = nullptr, m_Address = nullptr;
	HWND m_aButtons[8]{};
	WNDPROC m_EditProc = nullptr, m_ButtonProc = nullptr;
	HFONT m_Font = nullptr, m_TitleFont = nullptr, m_SmallFont = nullptr;
	HBRUSH m_AddressBrush = nullptr;
	HWND m_Hovered = nullptr;
	SUiTheme m_Theme;
	COLORREF m_Surface = RGB(20, 20, 20), m_AddressColor = RGB(30, 30, 30), m_Border = RGB(50, 50, 50);
	COLORREF m_Text = RGB(255, 255, 255), m_Muted = RGB(160, 160, 160), m_Accent = RGB(128, 128, 128);
	float m_aHover[8]{}, m_aPress[8]{};
	float m_GameControlAmount = 0, m_AddressFocus = 0;
	int m_Dpi = 96;
	bool m_Loading = false, m_NavigationFailed = false;
	UINT m_LoadTick = 0;
	bool m_GameControlEnabled = false;
	bool m_Dragging = false;
	int m_ResizeEdge = HTNOWHERE;
	RECT m_ResizeOrigin{}, m_LastParentRect{};
	bool m_LayoutDirty = false, m_PositionDirty = false;
	bool m_AltHeld = false;
	bool m_PendingBrowserFocus = false;
	POINT m_DragOrigin{};
	POINT m_WindowOrigin{};
	HHOOK m_KeyboardHook = nullptr;
	bool m_ComInitialized = false;
	bool m_Starting = false;
	bool m_Stopped = false;
	static CImpl *s_pHookTarget;

	// Only reserve a bare Alt press while this client's browser is visible.
	// Post focus changes out of the hook: WebView2 COM calls can dispatch messages.
	static LRESULT CALLBACK KeyboardHookProc(int Code, WPARAM WParam, LPARAM LParam)
	{
		CImpl *pSelf = s_pHookTarget;
		if(Code == HC_ACTION && pSelf && pSelf->m_Visible && GetAncestor(GetForegroundWindow(), GA_ROOT) == pSelf->m_Parent)
		{
			const auto *pKey = reinterpret_cast<const KBDLLHOOKSTRUCT *>(LParam);
			if(pKey->vkCode == VK_LMENU || pKey->vkCode == VK_RMENU || pKey->vkCode == VK_MENU)
			{
				const bool Down = WParam == WM_KEYDOWN || WParam == WM_SYSKEYDOWN;
				if(Down && !pSelf->m_AltHeld && !(GetAsyncKeyState(VK_CONTROL) & 0x8000))
					PostMessageW(pSelf->m_Window, WM_APP + 1, 0, 0);
				pSelf->m_AltHeld = Down;
				return 1;
			}
		}
		return CallNextHookEx(pSelf ? pSelf->m_KeyboardHook : nullptr, Code, WParam, LParam);
	}

	void EnableAltHook()
	{
		if(m_KeyboardHook) return;
		s_pHookTarget = this;
		m_AltHeld = false;
		m_KeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleW(nullptr), 0);
	}

	void DisableAltHook()
	{
		if(m_KeyboardHook) UnhookWindowsHookEx(m_KeyboardHook);
		m_KeyboardHook = nullptr;
		m_AltHeld = false;
		if(s_pHookTarget == this) s_pHookTarget = nullptr;
	}

	void SetGameControl(bool Enabled)
	{
		m_GameControlEnabled = Enabled;
		m_PendingBrowserFocus = !Enabled;
		m_Dragging = false;
		m_ResizeEdge = HTNOWHERE;
		if(GetCapture() == m_Window) ReleaseCapture();
		// A disabled child keeps rendering and playing media, but cannot take
		// keyboard or mouse focus away from SDL until Alt is pressed again.
		if(m_Window) EnableWindow(m_Window, !Enabled);
		if(Enabled) SetFocus(m_Parent);
		InvalidateChrome();
	}

	void UpdatePointerTransform()
	{
		if(!m_Dragging && m_ResizeEdge == HTNOWHERE) return;
		POINT Cursor{}; GetCursorPos(&Cursor);
		if(m_Dragging)
		{
			POINT Position{m_WindowOrigin.x + Cursor.x - m_DragOrigin.x, m_WindowOrigin.y + Cursor.y - m_DragOrigin.y};
			MapWindowPoints(nullptr, m_Parent, &Position, 1);
			SetWindowPos(m_Window, nullptr, Position.x, Position.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		else
		{
			RECT Rect = m_ResizeOrigin, Parent{};
			GetClientRect(m_Parent, &Parent);
			const int Dx = Cursor.x - m_DragOrigin.x, Dy = Cursor.y - m_DragOrigin.y;
			const bool Left = m_ResizeEdge == HTLEFT || m_ResizeEdge == HTTOPLEFT || m_ResizeEdge == HTBOTTOMLEFT;
			const bool Right = m_ResizeEdge == HTRIGHT || m_ResizeEdge == HTTOPRIGHT || m_ResizeEdge == HTBOTTOMRIGHT;
			const bool Top = m_ResizeEdge == HTTOP || m_ResizeEdge == HTTOPLEFT || m_ResizeEdge == HTTOPRIGHT;
			const bool Bottom = m_ResizeEdge == HTBOTTOM || m_ResizeEdge == HTBOTTOMLEFT || m_ResizeEdge == HTBOTTOMRIGHT;
			if(Left) Rect.left = std::clamp(Rect.left + Dx, 0L, std::max(0L, Rect.right - Px(480)));
			if(Right) Rect.right = std::clamp(Rect.right + Dx, std::min(Parent.right, Rect.left + Px(480)), Parent.right);
			if(Top) Rect.top = std::clamp(Rect.top + Dy, 0L, std::max(0L, Rect.bottom - Px(300)));
			if(Bottom) Rect.bottom = std::clamp(Rect.bottom + Dy, std::min(Parent.bottom, Rect.top + Px(300)), Parent.bottom);
			SetWindowPos(m_Window, nullptr, Rect.left, Rect.top, Rect.right - Rect.left, Rect.bottom - Rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		m_PositionDirty = true;
	}
	int Px(int Value) const { return MulDiv(Value, m_Dpi, 96); }

	RECT AddressRect() const
	{
		RECT Rect{}; GetClientRect(m_Window, &Rect);
		return {Px(134), Px(52), Rect.right - Px(104), Px(88)};
	}

	void InvalidateChrome()
	{
		if(!m_Window) return;
		RECT Rect{}; GetClientRect(m_Window, &Rect);
		RECT Header{0, 0, Rect.right, Px(TOOLBAR_HEIGHT)};
		RECT Footer{0, Rect.bottom - Px(STATUS_HEIGHT), Rect.right, Rect.bottom};
		InvalidateRect(m_Window, &Header, FALSE);
		InvalidateRect(m_Window, &Footer, FALSE);
	}

	void Paint(HDC Dc)
	{
		RECT Rect{}; GetClientRect(m_Window, &Rect);
		const auto Brush = CreateSolidBrush(m_Surface);
		FillRect(Dc, &Rect, Brush); DeleteObject(Brush);
		FillRounded(Dc, {Px(14), Px(10), Px(36), Px(32)}, m_AddressColor, Px(22), m_AddressColor);
		DrawTextLabel(Dc, m_TitleFont, m_Text, {Px(14), Px(10), Px(36), Px(32)}, L"X", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		DrawTextLabel(Dc, m_TitleFont, m_Text, {Px(46), Px(5), Rect.right - Px(352), Px(38)}, L"Xyrion 浏览器");
		if(Rect.right > Px(760))
			DrawTextLabel(Dc, m_SmallFont, m_Muted, {Px(240), Px(5), Rect.right - Px(204), Px(38)}, L"浏览网页 · 随时回到游戏", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		const RECT AddressBox = AddressRect();
		FillRounded(Dc, AddressBox, m_AddressColor, Px(36), BlendNative(m_Border, NativeColor(m_Theme.m_BorderFocused), m_AddressFocus));
		FillRounded(Dc, {Px(12), Px(52), Px(124), Px(88)}, m_AddressColor, Px(36), m_AddressColor);
		DrawTextLabel(Dc, m_SmallFont, m_Muted, {AddressBox.left + Px(11), AddressBox.top, AddressBox.left + Px(48), AddressBox.bottom}, L"URL");
		const int StatusY = Rect.bottom - Px(STATUS_HEIGHT);
		const auto Pen = CreatePen(PS_SOLID, 1, m_Border);
		const auto OldPen = SelectObject(Dc, Pen);
		MoveToEx(Dc, Px(1), Px(TOOLBAR_HEIGHT - 1), nullptr); LineTo(Dc, Rect.right - Px(1), Px(TOOLBAR_HEIGHT - 1));
		MoveToEx(Dc, Px(1), StatusY, nullptr); LineTo(Dc, Rect.right - Px(1), StatusY);
		SelectObject(Dc, OldPen); DeleteObject(Pen);
		const COLORREF StatusColor = m_NavigationFailed ? RGB(247, 158, 135) : m_Accent;
		FillRounded(Dc, {Px(14), StatusY + Px(10), Px(20), StatusY + Px(16)}, StatusColor, Px(6), StatusColor);
		DrawTextLabel(Dc, m_SmallFont, m_Muted, {Px(28), StatusY, Rect.right - Px(175), Rect.bottom},
			m_Starting ? L"正在启动浏览器…" : m_Loading ? L"正在加载网页…" : m_NavigationFailed ? L"网页加载失败，可刷新重试" : L"就绪");
		DrawTextLabel(Dc, m_SmallFont, m_Muted, {Rect.right - Px(168), StatusY, Rect.right - Px(14), Rect.bottom}, m_GameControlEnabled ? L"Alt  操作浏览器" : L"Alt  操作游戏", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		if(m_Loading || m_Starting)
		{
			const int Width = std::max(1L, Rect.right - Px(2));
			const int Segment = std::max(Px(40), Width / 5);
			const int Left = static_cast<int>((m_LoadTick * Px(12)) % (Width + Segment)) - Segment;
			RECT Progress{std::max(Px(1), Left), Px(TOOLBAR_HEIGHT - 2), std::min(Width, Left + Segment), Px(TOOLBAR_HEIGHT)};
			const auto ProgressBrush = CreateSolidBrush(m_Accent); FillRect(Dc, &Progress, ProgressBrush); DeleteObject(ProgressBrush);
		}
		const auto FrameBrush = CreateSolidBrush(m_Border); FrameRect(Dc, &Rect, FrameBrush); DeleteObject(FrameBrush);
	}

	void PaintButton(const DRAWITEMSTRUCT &Item)
	{
		const int Id = Item.CtlID;
		const bool Disabled = (Item.itemState & ODS_DISABLED) != 0;
		const bool Primary = Id == 104;
		const bool Close = Id == 107;
		const bool GameControl = Id == 108;
		const auto Brush = CreateSolidBrush(Id <= 103 ? m_AddressColor : m_Surface); FillRect(Item.hDC, &Item.rcItem, Brush); DeleteObject(Brush);
		RECT Rect = Item.rcItem; InflateRect(&Rect, -Px(1), -Px(1));
		const int Index = std::clamp(Id - 101, 0, 7);
		const COLORREF Base = Id <= 103 ? m_AddressColor : m_Surface;
		const ColorRGBA Surface = CompositeUiSurface(m_Theme.m_Surface);
		const COLORREF Feedback = NativeColor(CompositeUiSurface(ResolveUiIconButtonFeedback(Surface, !Disabled, true, false), Surface));
		COLORREF Background = BlendNative(Base, Feedback, m_aHover[Index]);
		if(Primary)
			Background = NativeColor(CompositeUiSurface(m_Theme.m_Selected.WithAlpha(0.42f), Surface));
		if(Close)
			Background = BlendNative(Base, RGB(133, 33, 33), m_aHover[Index]);
		Background = BlendNative(Background, NativeColor(CompositeUiSurface(ResolveUiIconButtonFeedback(Surface, true, true, true), Surface)), m_aPress[Index] * 0.6f);
		FillRounded(Item.hDC, Rect, Background, Px(36), Background);
		const COLORREF Foreground = Disabled ? BlendNative(Base, m_Text, 0.35f) : m_Text;

		if(Id == 104 || Id == 105 || GameControl)
		{
			if(GameControl)
			{
				RECT Label = Rect; Label.right -= Px(44);
				DrawTextLabel(Item.hDC, m_SmallFont, Foreground, Label, L"回到游戏", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				const RECT Track{Rect.right - Px(40), (Rect.top + Rect.bottom) / 2 - Px(8), Rect.right - Px(8), (Rect.top + Rect.bottom) / 2 + Px(8)};
				const COLORREF TrackColor = BlendNative(m_Border, m_Accent, m_GameControlAmount);
				FillRounded(Item.hDC, Track, TrackColor, Px(16), TrackColor);
				const int X = Track.left + Px(2) + round_to_int(Px(16) * m_GameControlAmount);
				FillRounded(Item.hDC, {X, Track.top + Px(2), X + Px(12), Track.bottom - Px(2)}, RGB(255,255,255), Px(12), RGB(255,255,255));
			}
			else
				DrawTextLabel(Item.hDC, m_Font, Foreground, Rect, Id == 104 ? L"前往  →" : L"返回游戏", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
		else
		{
			const int X = (Rect.left + Rect.right) / 2, Y = (Rect.top + Rect.bottom) / 2, R = Px(6);
			const auto Pen = CreatePen(PS_SOLID, Px(2), Foreground);
			const auto OldPen = SelectObject(Item.hDC, Pen);
			if(Id == 101 || Id == 102)
			{
				const int Direction = Id == 101 ? -1 : 1;
				MoveToEx(Item.hDC, X - Direction * R, Y, nullptr); LineTo(Item.hDC, X + Direction * R, Y);
				MoveToEx(Item.hDC, X, Y - R, nullptr); LineTo(Item.hDC, X + Direction * R, Y); LineTo(Item.hDC, X, Y + R);
			}
			else if(Id == 103)
			{
				Arc(Item.hDC, X - R, Y - R, X + R, Y + R, X + R, Y, X, Y - R);
				MoveToEx(Item.hDC, X + R, Y - R, nullptr); LineTo(Item.hDC, X + R, Y); LineTo(Item.hDC, X + Px(1), Y);
			}
			else if(Id == 106) { MoveToEx(Item.hDC, X - R, Y + Px(2), nullptr); LineTo(Item.hDC, X + R, Y + Px(2)); }
			else if(Id == 107)
			{
				MoveToEx(Item.hDC, X - Px(4), Y - Px(4), nullptr); LineTo(Item.hDC, X + Px(5), Y + Px(5));
				MoveToEx(Item.hDC, X + Px(4), Y - Px(4), nullptr); LineTo(Item.hDC, X - Px(5), Y + Px(5));
			}
			SelectObject(Item.hDC, OldPen); DeleteObject(Pen);
		}
		if((Item.itemState & ODS_FOCUS) && !(Item.itemState & ODS_NOFOCUSRECT)) { InflateRect(&Rect, -Px(4), -Px(4)); DrawFocusRect(Item.hDC, &Rect); }
	}

	static LRESULT CALLBACK ControlProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		auto *pSelf = reinterpret_cast<CImpl *>(GetWindowLongPtrW(Window, GWLP_USERDATA));
		if(Message == WM_MOUSEMOVE && pSelf->m_Hovered != Window)
		{
			const HWND Previous = pSelf->m_Hovered;
			pSelf->m_Hovered = Window;
			if(Previous) InvalidateRect(Previous, nullptr, FALSE);
			InvalidateRect(Window, nullptr, FALSE);
			TRACKMOUSEEVENT Track{sizeof(Track), TME_LEAVE, Window, 0}; TrackMouseEvent(&Track);
		}
		if(Message == WM_MOUSELEAVE)
		{
			if(pSelf->m_Hovered == Window) pSelf->m_Hovered = nullptr;
			InvalidateRect(Window, nullptr, FALSE);
		}
		if(Message == WM_SETFOCUS || Message == WM_KILLFOCUS) pSelf->InvalidateChrome();
		if(Window == pSelf->m_Address && Message == WM_KEYDOWN && WParam == VK_RETURN)
		{
			pSelf->Navigate();
			return 0;
		}
		if(Window == pSelf->m_Address && Message == WM_CHAR && WParam == VK_RETURN) return 0;
		if(Window == pSelf->m_Address && Message == WM_KEYDOWN && WParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
		{
			SendMessageW(Window, EM_SETSEL, 0, -1); return 0;
		}
		if(Message == WM_KEYDOWN && WParam == VK_F8) { pSelf->Hide(); return 0; }
		return CallWindowProcW(Window == pSelf->m_Address ? pSelf->m_EditProc : pSelf->m_ButtonProc, Window, Message, WParam, LParam);
	}

	static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		auto *pSelf = reinterpret_cast<CImpl *>(GetWindowLongPtrW(Window, GWLP_USERDATA));
		if(Message == WM_NCCREATE)
		{
			pSelf = static_cast<CImpl *>(reinterpret_cast<CREATESTRUCTW *>(LParam)->lpCreateParams);
			SetWindowLongPtrW(Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pSelf));
		}
		if(!pSelf) return DefWindowProcW(Window, Message, WParam, LParam);
		switch(Message)
		{
		case WM_NCCALCSIZE:
			return 0;
		case WM_NCPAINT:
			return 0;
		case WM_NCHITTEST:
		{
			POINT Point{GET_X_LPARAM(LParam), GET_Y_LPARAM(LParam)};
			ScreenToClient(Window, &Point);
			RECT Rect{}; GetClientRect(Window, &Rect);
			const int Edge = pSelf->Px(6);
			const bool Left = Point.x < Edge, Right = Point.x >= Rect.right - Edge;
			const bool Top = Point.y < Edge, Bottom = Point.y >= Rect.bottom - Edge;
			if(Top) return Left ? HTTOPLEFT : Right ? HTTOPRIGHT : HTTOP;
			if(Bottom) return Left ? HTBOTTOMLEFT : Right ? HTBOTTOMRIGHT : HTBOTTOM;
			if(Left) return HTLEFT;
			if(Right) return HTRIGHT;
			return HTCLIENT;
		}
		case WM_APP + 1:
			pSelf->SetGameControl(!pSelf->m_GameControlEnabled);
			return 0;
		case WM_NCLBUTTONDOWN:
			if(WParam >= HTLEFT && WParam <= HTBOTTOMRIGHT)
			{
				pSelf->m_ResizeEdge = static_cast<int>(WParam);
				GetCursorPos(&pSelf->m_DragOrigin);
				GetWindowRect(Window, &pSelf->m_ResizeOrigin);
				MapWindowPoints(nullptr, pSelf->m_Parent, reinterpret_cast<POINT *>(&pSelf->m_ResizeOrigin), 2);
				SetCapture(Window);
				return 0; // Never enter DefWindowProc's blocking size/move loop.
			}
			break;
		case WM_SYSCOMMAND:
			if((WParam & 0xfff0) == SC_SIZE || (WParam & 0xfff0) == SC_MOVE) return 0;
			break;
		case WM_LBUTTONDOWN:
		{
			POINT Point{GET_X_LPARAM(LParam), GET_Y_LPARAM(LParam)};
			if(Point.y < pSelf->Px(TITLE_HEIGHT))
			{
				pSelf->m_Dragging = true; GetCursorPos(&pSelf->m_DragOrigin);
				RECT WindowRect{}; GetWindowRect(Window, &WindowRect); pSelf->m_WindowOrigin = {WindowRect.left, WindowRect.top};
				SetCapture(Window); return 0;
			}
			break;
		}
		case WM_MOUSEMOVE:
			// Apply at most one geometry update per game frame.
			if(pSelf->m_Dragging || pSelf->m_ResizeEdge != HTNOWHERE) return 0;
			break;
		case WM_LBUTTONUP:
			if(pSelf->m_Dragging || pSelf->m_ResizeEdge != HTNOWHERE)
			{
				pSelf->UpdatePointerTransform();
				pSelf->m_Dragging = false;
				pSelf->m_ResizeEdge = HTNOWHERE;
				ReleaseCapture(); return 0;
			}
			break;
		case WM_CAPTURECHANGED:
		case WM_CANCELMODE:
			pSelf->m_Dragging = false;
			pSelf->m_ResizeEdge = HTNOWHERE;
			break;
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT Paint{}; const HDC Dc = BeginPaint(Window, &Paint);
			RECT Rect{}; GetClientRect(Window, &Rect);
			const HDC Buffer = CreateCompatibleDC(Dc);
			const auto Bitmap = CreateCompatibleBitmap(Dc, std::max(1L, Rect.right), std::max(1L, Rect.bottom));
			const auto Previous = SelectObject(Buffer, Bitmap);
			pSelf->Paint(Buffer);
			BitBlt(Dc, 0, 0, Rect.right, Rect.bottom, Buffer, 0, 0, SRCCOPY);
			SelectObject(Buffer, Previous); DeleteObject(Bitmap); DeleteDC(Buffer);
			EndPaint(Window, &Paint); return 0;
		}
		case WM_DRAWITEM:
			pSelf->PaintButton(*reinterpret_cast<DRAWITEMSTRUCT *>(LParam)); return TRUE;
		case WM_CTLCOLOREDIT:
			SetTextColor(reinterpret_cast<HDC>(WParam), pSelf->m_Text);
			SetBkColor(reinterpret_cast<HDC>(WParam), pSelf->m_AddressColor);
			return reinterpret_cast<LRESULT>(pSelf->m_AddressBrush);
		case WM_TIMER:
			if(WParam == 1 && (pSelf->m_Loading || pSelf->m_Starting)) { ++pSelf->m_LoadTick; pSelf->InvalidateChrome(); }
			return 0;
		case WM_CLOSE:
			pSelf->Hide();
			return 0;
		case WM_GETMINMAXINFO:
			reinterpret_cast<MINMAXINFO *>(LParam)->ptMinTrackSize = {pSelf->Px(480), pSelf->Px(300)};
			return 0;
		case WM_MOVE:
			pSelf->m_PositionDirty = true;
			return 0;
		case WM_SIZE:
			pSelf->m_LayoutDirty = true;
			InvalidateRect(Window, nullptr, FALSE);
			return 0;
		case WM_COMMAND:
			if(LOWORD(WParam) == 104) pSelf->Navigate();
			else if(LOWORD(WParam) == 105) pSelf->SetGameControl(true);
			else if(LOWORD(WParam) == 107) pSelf->Hide();
			else if(LOWORD(WParam) == 106) ShowWindow(Window, SW_MINIMIZE);
			else if(LOWORD(WParam) == 108) pSelf->SetGameControl(!pSelf->m_GameControlEnabled);
			else if(pSelf->m_pWebView)
			{
				if(LOWORD(WParam) == 101) pSelf->m_pWebView->GoBack();
				if(LOWORD(WParam) == 102) pSelf->m_pWebView->GoForward();
				if(LOWORD(WParam) == 103) pSelf->m_pWebView->Reload();
			}
			return 0;
		case WM_KEYDOWN:
			if(WParam == VK_F8 || WParam == VK_ESCAPE) { pSelf->Hide(); return 0; }
			break;
		}
		return DefWindowProcW(Window, Message, WParam, LParam);
	}

	void Layout()
	{
		if(!m_Window) return;
		RECT Rect{};
		GetClientRect(m_Window, &Rect);
		// The native host uses the same rounded silhouette as Qm secondary panels.
		SetWindowRgn(m_Window, CreateRoundRectRgn(0, 0, Rect.right + 1, Rect.bottom + 1, Px(20), Px(20)), TRUE);
		for(int i = 0; i < 3; ++i) if(m_aButtons[i]) MoveWindow(m_aButtons[i], Px(12 + i * 39), Px(52), Px(34), Px(36), TRUE);
		const RECT AddressBox = AddressRect();
		if(m_Address) MoveWindow(m_Address, AddressBox.left + Px(48), AddressBox.top + Px(8), std::max(Px(20), static_cast<int>(AddressBox.right - AddressBox.left) - Px(60)), Px(22), TRUE);
		if(m_aButtons[3]) MoveWindow(m_aButtons[3], Rect.right - Px(92), Px(52), Px(78), Px(36), TRUE);
		if(m_aButtons[4]) MoveWindow(m_aButtons[4], Rect.right - Px(180), Px(7), Px(84), Px(28), TRUE);
		if(m_aButtons[5]) MoveWindow(m_aButtons[5], Rect.right - Px(86), Px(7), Px(32), Px(28), TRUE);
		if(m_aButtons[6]) MoveWindow(m_aButtons[6], Rect.right - Px(46), Px(7), Px(32), Px(28), TRUE);
		if(m_aButtons[7]) MoveWindow(m_aButtons[7], Rect.right - Px(338), Px(7), Px(145), Px(28), TRUE);
		for(HWND Button : m_aButtons)
			if(Button)
			{
				RECT Bounds{};
				GetClientRect(Button, &Bounds);
				SetWindowRgn(Button, CreateRoundRectRgn(0, 0, Bounds.right + 1, Bounds.bottom + 1, Bounds.bottom, Bounds.bottom), TRUE);
			}
		if(m_pController)
		{
			Rect.top = Px(TOOLBAR_HEIGHT); Rect.bottom = std::max(Rect.top, Rect.bottom - Px(STATUS_HEIGHT));
			Rect.left = Px(1); Rect.right -= Px(1);
			m_pController->put_Bounds(Rect);
		}
	}

	void Navigate()
	{
		if(!m_pWebView) return;
		wchar_t aUrl[1024]{};
		GetWindowTextW(m_Address, aUrl, std::size(aUrl));
		std::string Url = windows_wide_to_utf8(aUrl).value_or("");
		if(Url.empty()) return;
		if(Url.find("://") == std::string::npos) Url = "https://" + Url;
		// Only web navigation; do not expose local files or shell protocols.
		if(!str_startswith(Url.c_str(), "https://") && !str_startswith(Url.c_str(), "http://")) return;
		str_copy(g_Config.m_XcBrowserUrl, Url.c_str());
		m_Loading = true; m_NavigationFailed = false; InvalidateChrome();
		SetWindowTextW(m_Address, windows_utf8_to_wide(Url.c_str()).c_str());
		m_pWebView->Navigate(windows_utf8_to_wide(Url.c_str()).c_str());
		if(!m_GameControlEnabled) m_pController->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
	}

	void Fail(HRESULT Error)
	{
		char aError[180];
		str_format(aError, sizeof(aError), "浏览器无法启动 (0x%08x)，请检查 Microsoft Edge WebView2 Runtime", static_cast<unsigned>(Error));
		m_Error = aError;
		m_Starting = false;
		Hide();
	}

public:
	bool m_Visible = false;
	bool Interactive() const { return m_Visible && !m_GameControlEnabled && !IsIconic(m_Window) && !IsIconic(m_Parent); }
	void ApplyPendingFocus()
	{
		if(m_PendingBrowserFocus && Interactive() && m_pController && GetAncestor(GetForegroundWindow(), GA_ROOT) == m_Parent)
		{
			m_PendingBrowserFocus = false;
			SetFocus(m_Window);
			m_pController->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
		}
	}
	std::string m_Error;

	void Hide()
	{
		m_Visible = false;
		DisableAltHook();
		SetGameControl(false);
		if(m_pController) m_pController->put_IsVisible(FALSE);
		if(m_Window) ShowWindow(m_Window, SW_HIDE);
		if(m_Parent) SetFocus(m_Parent);
	}

	void Show()
	{
		if(m_Stopped) return;
		if(m_Window)
		{
			m_Visible = true;
			EnableAltHook();
			SetGameControl(false);
			ShowWindow(m_Window, SW_RESTORE);
			if(m_pController) { m_pController->put_IsVisible(TRUE); m_pController->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC); }
			return;
		}
		m_Parent = FindGameWindow();
		if(!m_Parent) { Fail(E_HANDLE); return; }
		using FGetDpi = UINT(WINAPI *)(HWND);
		const auto GetWindowDpi = std::bit_cast<FGetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
		m_Dpi = GetWindowDpi ? GetWindowDpi(m_Parent) : 96;
		if(!m_Dpi) m_Dpi = 96;
		m_Font = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
		m_TitleFont = CreateFontW(-Px(16), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		m_SmallFont = CreateFontW(-Px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
		SyncTheme();
		const HRESULT ComResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if(FAILED(ComResult)) { Fail(ComResult); return; }
		m_ComInitialized = true;
		static HMODULE s_Loader = []() {
			wchar_t aPath[32768]{};
			GetModuleFileNameW(nullptr, aPath, std::size(aPath));
			std::wstring Path(aPath);
			Path.resize(Path.find_last_of(L"\\/") + 1);
			Path += L"WebView2Loader.dll";
			return LoadLibraryExW(Path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		}();
		using FCreate = HRESULT(STDMETHODCALLTYPE *)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);
		const auto Create = s_Loader ? std::bit_cast<FCreate>(GetProcAddress(s_Loader, "CreateCoreWebView2EnvironmentWithOptions")) : nullptr;
		if(!Create) { Fail(HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND)); return; }
		static const wchar_t *s_pClass = L"XyrionWebBrowser";
		WNDCLASSW Class{};
		Class.hInstance = GetModuleHandleW(nullptr);
		Class.lpfnWndProc = WindowProc;
		Class.lpszClassName = s_pClass;
		Class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		Class.hbrBackground = nullptr;
		RegisterClassW(&Class);
		RECT Parent{};
		GetClientRect(m_Parent, &Parent);
		const int Width = std::min(std::max(Px(480), g_Config.m_XcBrowserW), static_cast<int>(Parent.right));
		const int Height = std::min(std::max(Px(300), g_Config.m_XcBrowserH), static_cast<int>(Parent.bottom));
		m_Window = CreateWindowExW(0, s_pClass, L"Xyrion 浏览器 — F8 返回游戏", WS_CHILD | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
			std::clamp<LONG>(g_Config.m_XcBrowserX, 0, std::max(0L, Parent.right - Width)), std::clamp<LONG>(g_Config.m_XcBrowserY, 0, std::max(0L, Parent.bottom - Height)), Width, Height, m_Parent, nullptr, Class.hInstance, this);
		if(!m_Window) { Fail(HRESULT_FROM_WIN32(GetLastError())); return; }
		const wchar_t *apLabels[] = {L"后退", L"前进", L"刷新", L"前往", L"返回游戏", L"最小化", L"关闭浏览器", L"游戏操控"};
		for(int i = 0; i < 8; ++i)
		{
			m_aButtons[i] = CreateWindowW(L"BUTTON", apLabels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, m_Window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(101 + i)), Class.hInstance, nullptr);
			SetWindowLongPtrW(m_aButtons[i], GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
			m_ButtonProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_aButtons[i], GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ControlProc)));
			SendMessageW(m_aButtons[i], WM_SETFONT, reinterpret_cast<WPARAM>(m_Font), TRUE);
		}
		EnableWindow(m_aButtons[0], FALSE); EnableWindow(m_aButtons[1], FALSE);
		m_Address = CreateWindowExW(0, L"EDIT", windows_utf8_to_wide(g_Config.m_XcBrowserUrl).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, m_Window, nullptr, Class.hInstance, nullptr);
		SendMessageW(m_Address, EM_SETLIMITTEXT, 1023, 0);
		SetWindowLongPtrW(m_Address, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
		m_EditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_Address, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ControlProc)));
		SendMessageW(m_Address, WM_SETFONT, reinterpret_cast<WPARAM>(m_Font), TRUE);
		SetTimer(m_Window, 1, 40, nullptr);
		m_Visible = true;
		EnableAltHook();
		m_Starting = true;
		ShowWindow(m_Window, SW_SHOW);
		Layout();
		wchar_t aLocal[32768]{};
		GetEnvironmentVariableW(L"LOCALAPPDATA", aLocal, std::size(aLocal));
		const std::wstring UserData = std::wstring(aLocal) + L"\\XyrionClient\\WebView2";
		const std::weak_ptr<CImpl> Weak = shared_from_this();
		auto *pHandler = new CHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment *>([Weak](HRESULT Result, ICoreWebView2Environment *pEnvironment) {
			auto Self = Weak.lock();
			if(!Self || Self->m_Stopped) return S_OK;
			if(FAILED(Result) || !pEnvironment) { Self->Fail(Result); return S_OK; }
			auto *pControllerHandler = new CHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller *>([Weak](HRESULT ControllerResult, ICoreWebView2Controller *pController) {
				auto State = Weak.lock();
				if(!State || State->m_Stopped) { if(pController) pController->Close(); return S_OK; }
				if(FAILED(ControllerResult) || !pController) { State->Fail(ControllerResult); return S_OK; }
				State->m_Starting = false;
				State->m_pController = pController;
				pController->AddRef();
				pController->get_CoreWebView2(&State->m_pWebView);
				State->Layout();
				pController->put_IsVisible(State->m_Visible);
				EventRegistrationToken Token{};
				auto *pStarting = new CHandler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *>([Weak](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *) {
					if(auto Browser = Weak.lock(); Browser && !Browser->m_Stopped)
					{
						Browser->m_Loading = true; Browser->m_NavigationFailed = false; Browser->InvalidateChrome();
					}
					return S_OK;
				});
				State->m_pWebView->add_NavigationStarting(pStarting, &Token);
				pStarting->Release();
				auto *pKey = new CHandler<ICoreWebView2AcceleratorKeyPressedEventHandler, ICoreWebView2Controller *, ICoreWebView2AcceleratorKeyPressedEventArgs *>([Weak](ICoreWebView2Controller *, ICoreWebView2AcceleratorKeyPressedEventArgs *pArgs) {
					UINT Key = 0; COREWEBVIEW2_KEY_EVENT_KIND Kind{};
					pArgs->get_VirtualKey(&Key); pArgs->get_KeyEventKind(&Kind);
					if(Key == VK_F8 && Kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN) { pArgs->put_Handled(TRUE); if(auto Browser = Weak.lock()) PostMessageW(Browser->m_Window, WM_CLOSE, 0, 0); }
					return S_OK;
				});
				pController->add_AcceleratorKeyPressed(pKey, &Token);
				pKey->Release();
				auto *pNavigation = new CHandler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *>([Weak](ICoreWebView2 *pView, ICoreWebView2NavigationCompletedEventArgs *pArgs) {
					if(auto Browser = Weak.lock(); Browser && !Browser->m_Stopped)
					{
						BOOL Success = FALSE; pArgs->get_IsSuccess(&Success);
						Browser->m_Loading = false; Browser->m_NavigationFailed = !Success;
						BOOL Back = FALSE, Forward = FALSE;
						pView->get_CanGoBack(&Back); pView->get_CanGoForward(&Forward);
						EnableWindow(Browser->m_aButtons[0], Back); EnableWindow(Browser->m_aButtons[1], Forward);
						Browser->InvalidateChrome();
						LPWSTR pSource = nullptr;
						if(SUCCEEDED(pView->get_Source(&pSource)) && pSource)
						{
							const auto Source = windows_wide_to_utf8(pSource);
							if(Source && (str_startswith(Source->c_str(), "https://") || str_startswith(Source->c_str(), "http://"))) { str_copy(g_Config.m_XcBrowserUrl, Source->c_str()); SetWindowTextW(Browser->m_Address, pSource); }
							CoTaskMemFree(pSource);
						}
					}
					return S_OK;
				});
				State->m_pWebView->add_NavigationCompleted(pNavigation, &Token);
				pNavigation->Release();
				auto *pNewWindow = new CHandler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *>([Weak](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *pArgs) {
					pArgs->put_Handled(TRUE);
					LPWSTR pUri = nullptr;
					if(SUCCEEDED(pArgs->get_Uri(&pUri)) && pUri)
					{
						if(auto Browser = Weak.lock(); Browser && !Browser->m_Stopped) { SetWindowTextW(Browser->m_Address, pUri); Browser->Navigate(); }
						CoTaskMemFree(pUri);
					}
					return S_OK;
				});
				State->m_pWebView->add_NewWindowRequested(pNewWindow, &Token);
				pNewWindow->Release();
				State->Navigate();
				if(!State->m_Visible) State->Hide();
				return S_OK;
			});
			const HRESULT Started = pEnvironment->CreateCoreWebView2Controller(Self->m_Window, pControllerHandler);
			pControllerHandler->Release();
			if(FAILED(Started)) Self->Fail(Started);
			return S_OK;
		});
		const HRESULT Started = Create(nullptr, UserData.c_str(), nullptr, pHandler);
		pHandler->Release();
		if(FAILED(Started)) Fail(Started);
	}

	void SyncTheme()
	{
		m_Theme = ResolveUiTheme(ColorHSLA(g_Config.m_QmUiColor), g_Config.m_QmUiOpacity / 100.0f, ColorHSLA(g_Config.m_QmUiFocusColor), ColorHSLA(g_Config.m_QmUiAccentColor).WithAlpha(g_Config.m_QmUiAccentOpacity / 100.0f), ColorHSLA(g_Config.m_QmUiSelectedColor));
		const ColorRGBA Surface = CompositeUiSurface(m_Theme.m_Surface);
		m_Surface = NativeColor(Surface);
		m_Text = NativeColor(m_Theme.m_TextBody);
		m_Muted = NativeColor(CompositeUiSurface(m_Theme.m_TextSmall, Surface));
		m_Border = NativeColor(CompositeUiSurface(m_Theme.m_Border, Surface));
		m_Accent = NativeColor(CompositeUiSurface(m_Theme.m_Accent, Surface));
		const COLORREF Address = NativeColor(CompositeUiSurface(m_Theme.m_InputSurface, Surface));
		if(!m_AddressBrush || Address != m_AddressColor)
		{
			if(m_AddressBrush) DeleteObject(m_AddressBrush);
			m_AddressColor = Address;
			m_AddressBrush = CreateSolidBrush(Address);
		}
	}
public:
	void UpdateStyle(CUiV2AnimationRuntime &Runtime)
	{
		if(!m_Visible || !m_Window) return;
		const COLORREF Previous[] = {m_Surface, m_Text, m_Muted, m_Border, m_Accent, m_AddressColor};
		SyncTheme();
		bool Changed = Previous[0] != m_Surface || Previous[1] != m_Text || Previous[2] != m_Muted || Previous[3] != m_Border || Previous[4] != m_Accent || Previous[5] != m_AddressColor;
		const float Duration = g_Config.m_QmUiMotionLevel == 0 ? 0.0f : g_Config.m_QmUiMotionLevel == 1 ? 0.06f : 0.12f;
		const auto Animate = [&](float &Value, uint64_t Id, float Target) {
			const float Next = ResolveUiAnimValue(Runtime, BuildUiAnimNodeKey(str_quickhash("browser-qm"), Id), EUiAnimProperty::SCALE, Target, Duration, EEasing::EASE_OUT);
			const bool Updated = std::abs(Next - Value) > 0.001f;
			Value = Next;
			return Updated;
		};
		Changed |= Animate(m_GameControlAmount, 1, m_GameControlEnabled ? 1.0f : 0.0f);
		Changed |= Animate(m_AddressFocus, 2, GetFocus() == m_Address ? 1.0f : 0.0f);
		for(int i = 0; i < 8; ++i)
		{
			const bool Enabled = IsWindowEnabled(m_aButtons[i]);
			const bool Hover = Enabled && m_Hovered == m_aButtons[i];
			const bool Down = Enabled && (SendMessageW(m_aButtons[i], BM_GETSTATE, 0, 0) & BST_PUSHED);
			bool ButtonChanged = Animate(m_aHover[i], 10 + i, Hover ? 1.0f : 0.0f);
			ButtonChanged |= Animate(m_aPress[i], 20 + i, Down ? 1.0f : 0.0f);
			if(Changed || ButtonChanged) InvalidateRect(m_aButtons[i], nullptr, FALSE);
		}
		if(Changed)
		{
			InvalidateChrome();
			InvalidateRect(m_Address, nullptr, FALSE);
		}
	}
	void Update()
	{
		if(!m_Visible || !m_Window || !IsWindow(m_Window) || !m_Parent || !IsWindow(m_Parent)) return;
		if(IsIconic(m_Parent))
		{
			m_Dragging = false;
			m_ResizeEdge = HTNOWHERE;
			if(GetCapture() == m_Window) ReleaseCapture();
			return;
		}
		if(GetAncestor(GetForegroundWindow(), GA_ROOT) != m_Parent) m_AltHeld = false;
		if(IsIconic(m_Window)) return;
		UpdatePointerTransform();
		RECT Rect{}, Parent{};
		GetWindowRect(m_Window, &Rect);
		MapWindowPoints(nullptr, m_Parent, reinterpret_cast<POINT *>(&Rect), 2);
		GetClientRect(m_Parent, &Parent);
		const int Width = std::min(Rect.right - Rect.left, Parent.right);
		const int Height = std::min(Rect.bottom - Rect.top, Parent.bottom);
		const int X = std::clamp(Rect.left, 0L, std::max(0L, Parent.right - Width));
		const int Y = std::clamp(Rect.top, 0L, std::max(0L, Parent.bottom - Height));
		if(X != Rect.left || Y != Rect.top || Width != Rect.right - Rect.left || Height != Rect.bottom - Rect.top)
			MoveWindow(m_Window, X, Y, Width, Height, TRUE);
		g_Config.m_XcBrowserX = X; g_Config.m_XcBrowserY = Y;
		g_Config.m_XcBrowserW = Width; g_Config.m_XcBrowserH = Height;
		if(m_LayoutDirty)
		{
			m_LayoutDirty = false;
			Layout();
		}
		RECT ParentScreen{}; GetWindowRect(m_Parent, &ParentScreen);
		if(!EqualRect(&ParentScreen, &m_LastParentRect)) { m_LastParentRect = ParentScreen; m_PositionDirty = true; }
		if(m_PositionDirty && m_pController)
		{
			m_PositionDirty = false;
			m_pController->NotifyParentWindowPositionChanged();
		}
	}

	void Shutdown()
	{
		m_Stopped = true;
		DisableAltHook();
		SetGameControl(false);
		if(m_pController) { m_pController->Close(); m_pController->Release(); m_pController = nullptr; }
		if(m_pWebView) { m_pWebView->Release(); m_pWebView = nullptr; }
		if(m_Window) { DestroyWindow(m_Window); m_Window = nullptr; }
		if(m_Font) { DeleteObject(m_Font); m_Font = nullptr; }
		if(m_TitleFont) { DeleteObject(m_TitleFont); m_TitleFont = nullptr; }
		if(m_SmallFont) { DeleteObject(m_SmallFont); m_SmallFont = nullptr; }
		if(m_AddressBrush) { DeleteObject(m_AddressBrush); m_AddressBrush = nullptr; }
		if(m_ComInitialized) { CoUninitialize(); m_ComInitialized = false; }
	}
	~CImpl() { Shutdown(); }
};
CWebBrowser::CImpl *CWebBrowser::CImpl::s_pHookTarget = nullptr;
#else
class CWebBrowser::CImpl
{
public:
	bool m_Visible = false;
	std::string m_Error;
	bool Interactive() const { return false; }
	void ApplyPendingFocus() {}
	void Show() { m_Error = "内置浏览器目前支持 Windows WebView2"; }
	void Hide() { m_Visible = false; }
	void Update() {}
	void Shutdown() {}
};
#endif

CWebBrowser::CWebBrowser() = default;
CWebBrowser::~CWebBrowser() = default;

void CWebBrowser::ConToggle(IConsole::IResult *, void *pUserData) { static_cast<CWebBrowser *>(pUserData)->Toggle(); }
void CWebBrowser::OnConsoleInit() { Console()->Register("toggle_browser", "", CFGFLAG_CLIENT, ConToggle, this, "打开或关闭内置浏览器"); }
void CWebBrowser::OnInit() { GameClient()->m_Binds.Bind(KEY_F8, "toggle_browser", true); }

void CWebBrowser::Toggle()
{
	if(!m_pImpl) m_pImpl = std::make_shared<CImpl>();
	if(m_pImpl->m_Visible) { m_pImpl->Hide(); return; }
	if(g_Config.m_GfxFullscreen == 1)
	{
		m_RestoreFullscreen = true;
		g_Config.m_GfxFullscreen = 2;
		Graphics()->SetWindowParams(2, g_Config.m_GfxBorderless);
	}
	for(int i = 0; i < 2; ++i) GameClient()->m_Controls.ResetInput(i);
	Input()->MouseModeAbsolute();
	Graphics()->SetWindowGrab(false);
	m_pImpl->Show();
}

void CWebBrowser::OnUpdate()
{
	if(!m_pImpl) return;
#if defined(CONF_FAMILY_WINDOWS)
	m_pImpl->UpdateStyle(GameClient()->UiRuntimeV2()->AnimRuntime());
#endif
	m_pImpl->Update();
	const bool Failed = !m_pImpl->m_Error.empty();
	if(Failed)
	{
		GameClient()->m_Chat.Echo(m_pImpl->m_Error.c_str());
		Client()->AddWarning(SWarning("内置浏览器", m_pImpl->m_Error.c_str()));
		m_pImpl->Shutdown();
	}
	const bool Interactive = m_pImpl->Interactive();
	if(Interactive != m_WasInteractive)
	{
		if(Interactive) GameClient()->m_TeeBalance.Stop("已切换到浏览器操作");
		for(int Slot = 0; Slot < NUM_DUMMIES; ++Slot)
		{
			GameClient()->m_Controls.m_aLastData[Slot].m_Hook = 0;
			GameClient()->m_Controls.ResetInput(Slot);
		}
		if(Interactive) { Input()->MouseModeAbsolute(); Graphics()->SetWindowGrab(false); }
		else Input()->MouseModeRelative();
		m_WasInteractive = Interactive;
	}
	if(Interactive) { Input()->MouseModeAbsolute(); Graphics()->SetWindowGrab(false); }
	else if(!m_pImpl->m_Visible && (m_WasVisible || m_RestoreFullscreen || Failed))
	{
		if(m_RestoreFullscreen && g_Config.m_GfxFullscreen == 2) { g_Config.m_GfxFullscreen = 1; Graphics()->SetWindowParams(1, g_Config.m_GfxBorderless); }
		m_RestoreFullscreen = false;
		// Both the game and its menus use SDL relative motion for the cursor.
		Input()->MouseModeRelative();
	}
	m_WasVisible = m_pImpl->m_Visible;
	if(!Failed) m_pImpl->ApplyPendingFocus();
	if(Failed) m_pImpl.reset();
}

void CWebBrowser::OnShutdown()
{
	if(m_pImpl) { m_pImpl->Shutdown(); m_pImpl.reset(); }
	if(m_RestoreFullscreen && g_Config.m_GfxFullscreen == 2) g_Config.m_GfxFullscreen = 1;
	m_RestoreFullscreen = false;
}

bool CWebBrowser::OnInput(const IInput::CEvent &Event)
{
	if(!m_pImpl || !m_pImpl->Interactive()) return false;
	if(Event.m_Key == KEY_F8 && (Event.m_Flags & IInput::FLAG_PRESS)) Toggle();
	return true;
}

bool CWebBrowser::OnCursorMove(float, float, IInput::ECursorType)
{
	return m_pImpl && m_pImpl->Interactive();
}
