#include "web_browser.h"

#include <base/str.h>
#include <base/log.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <game/client/gameclient.h>

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
	constexpr COLORREF SURFACE = RGB(19, 24, 35);
	constexpr COLORREF ADDRESS = RGB(30, 38, 53);
	constexpr COLORREF BORDER = RGB(49, 62, 81);
	constexpr COLORREF TEXT = RGB(224, 233, 245);
	constexpr COLORREF MUTED = RGB(137, 157, 181);
	constexpr COLORREF ACCENT = RGB(94, 211, 237);

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
	int m_Dpi = 96;
	bool m_Loading = false, m_NavigationFailed = false;
	UINT m_LoadTick = 0;
	bool m_GameControlEnabled = false;
	bool m_Dragging = false;
	POINT m_DragOrigin{};
	POINT m_WindowOrigin{};
	HHOOK m_KeyboardHook = nullptr;
	bool m_aForwardedKeys[SDL_NUM_SCANCODES]{};
	bool m_ComInitialized = false;
	bool m_Starting = false;
	bool m_Stopped = false;
	static CImpl *s_pHookTarget;

	static SDL_Scancode ScancodeFromVirtualKey(DWORD Key)
	{
		if(Key >= 'A' && Key <= 'Z') return static_cast<SDL_Scancode>(SDL_SCANCODE_A + Key - 'A');
		if(Key >= '1' && Key <= '9') return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + Key - '1');
		switch(Key)
		{
		case '0': return SDL_SCANCODE_0;
		case VK_UP: return SDL_SCANCODE_UP;
		case VK_DOWN: return SDL_SCANCODE_DOWN;
		case VK_LEFT: return SDL_SCANCODE_LEFT;
		case VK_RIGHT: return SDL_SCANCODE_RIGHT;
		case VK_SPACE: return SDL_SCANCODE_SPACE;
		case VK_SHIFT: return SDL_SCANCODE_LSHIFT;
		case VK_CONTROL: return SDL_SCANCODE_LCTRL;
		case VK_MENU: return SDL_SCANCODE_LALT;
		case VK_TAB: return SDL_SCANCODE_TAB;
		case VK_RETURN: return SDL_SCANCODE_RETURN;
		case VK_BACK: return SDL_SCANCODE_BACKSPACE;
		case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
		default: return SDL_SCANCODE_UNKNOWN;
		}
	}

	static LRESULT CALLBACK KeyboardHookProc(int Code, WPARAM WParam, LPARAM LParam)
	{
		CImpl *pSelf = s_pHookTarget;
		if(Code == HC_ACTION && pSelf && pSelf->m_GameControlEnabled && pSelf->m_Window && IsWindow(pSelf->m_Window) && pSelf->m_Parent && IsWindow(pSelf->m_Parent))
		{
			const HWND Foreground = GetAncestor(GetForegroundWindow(), GA_ROOT);
			const auto *pKey = reinterpret_cast<const KBDLLHOOKSTRUCT *>(LParam);
			const SDL_Scancode Scan = ScancodeFromVirtualKey(pKey->vkCode);
			if(Foreground == pSelf->m_Parent && Scan != SDL_SCANCODE_UNKNOWN && pKey->vkCode != VK_F8 &&
				(WParam == WM_KEYDOWN || WParam == WM_SYSKEYDOWN || WParam == WM_KEYUP || WParam == WM_SYSKEYUP))
			{
				SDL_Event Event{};
				Event.type = (WParam == WM_KEYDOWN || WParam == WM_SYSKEYDOWN) ? SDL_KEYDOWN : SDL_KEYUP;
				Event.key.state = Event.type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
				Event.key.keysym.scancode = Scan;
				Event.key.keysym.sym = SDL_GetKeyFromScancode(Scan);
				Event.key.keysym.mod = SDL_GetModState();
				SDL_PushEvent(&Event);
				pSelf->m_aForwardedKeys[Scan] = Event.type == SDL_KEYDOWN;
			}
		}
		return CallNextHookEx(pSelf ? pSelf->m_KeyboardHook : nullptr, Code, WParam, LParam);
	}

	void SetGameControl(bool Enabled)
	{
		if(m_GameControlEnabled == Enabled) return;
		m_GameControlEnabled = Enabled;
		if(Enabled)
		{
			s_pHookTarget = this;
			m_KeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleW(nullptr), 0);
			if(!m_KeyboardHook)
			{
				m_GameControlEnabled = false;
				s_pHookTarget = nullptr;
				log_error("browser", "could not enable game input: %lu", GetLastError());
			}
		}
		else
		{
			if(m_KeyboardHook) UnhookWindowsHookEx(m_KeyboardHook);
			m_KeyboardHook = nullptr;
			if(s_pHookTarget == this) s_pHookTarget = nullptr;
			for(int i = 0; i < SDL_NUM_SCANCODES; ++i)
				if(m_aForwardedKeys[i])
				{
					SDL_Event Event{}; Event.type = SDL_KEYUP; Event.key.state = SDL_RELEASED;
					Event.key.keysym.scancode = static_cast<SDL_Scancode>(i); Event.key.keysym.sym = SDL_GetKeyFromScancode(Event.key.keysym.scancode);
					SDL_PushEvent(&Event); m_aForwardedKeys[i] = false;
				}
		}
		InvalidateChrome();
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
		const auto Brush = CreateSolidBrush(SURFACE);
		FillRect(Dc, &Rect, Brush); DeleteObject(Brush);
		FillRounded(Dc, {Px(14), Px(10), Px(36), Px(32)}, RGB(31, 66, 82), Px(10), RGB(43, 91, 109));
		DrawTextLabel(Dc, m_TitleFont, ACCENT, {Px(14), Px(10), Px(36), Px(32)}, L"X", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		DrawTextLabel(Dc, m_TitleFont, TEXT, {Px(46), Px(5), Rect.right - Px(500), Px(38)}, L"Xyrion Browser");
		if(Rect.right > Px(760))
			DrawTextLabel(Dc, m_SmallFont, MUTED, {Px(240), Px(5), Rect.right - Px(204), Px(38)}, L"浏览网页 · 随时回到游戏", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		const RECT AddressBox = AddressRect();
		const bool Focused = GetFocus() == m_Address;
		FillRounded(Dc, AddressBox, ADDRESS, Px(14), Focused ? ACCENT : BORDER);
		DrawTextLabel(Dc, m_SmallFont, MUTED, {AddressBox.left + Px(11), AddressBox.top, AddressBox.left + Px(48), AddressBox.bottom}, L"URL");
		const int StatusY = Rect.bottom - Px(STATUS_HEIGHT);
		const auto Pen = CreatePen(PS_SOLID, 1, BORDER);
		const auto OldPen = SelectObject(Dc, Pen);
		MoveToEx(Dc, Px(1), Px(TOOLBAR_HEIGHT - 1), nullptr); LineTo(Dc, Rect.right - Px(1), Px(TOOLBAR_HEIGHT - 1));
		MoveToEx(Dc, Px(1), StatusY, nullptr); LineTo(Dc, Rect.right - Px(1), StatusY);
		SelectObject(Dc, OldPen); DeleteObject(Pen);
		const COLORREF StatusColor = m_NavigationFailed ? RGB(247, 158, 135) : ACCENT;
		FillRounded(Dc, {Px(14), StatusY + Px(10), Px(20), StatusY + Px(16)}, StatusColor, Px(6), StatusColor);
		DrawTextLabel(Dc, m_SmallFont, MUTED, {Px(28), StatusY, Rect.right - Px(175), Rect.bottom},
			m_Starting ? L"正在启动浏览器…" : m_Loading ? L"正在加载网页…" : m_NavigationFailed ? L"网页加载失败，可刷新重试" : L"就绪");
		DrawTextLabel(Dc, m_SmallFont, MUTED, {Rect.right - Px(168), StatusY, Rect.right - Px(14), Rect.bottom}, m_GameControlEnabled ? L"游戏按键同步已开" : L"F8  返回游戏", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		if(m_Loading || m_Starting)
		{
			const int Width = std::max(1L, Rect.right - Px(2));
			const int Segment = std::max(Px(40), Width / 5);
			const int Left = static_cast<int>((m_LoadTick * Px(12)) % (Width + Segment)) - Segment;
			RECT Progress{std::max(Px(1), Left), Px(TOOLBAR_HEIGHT - 2), std::min(Width, Left + Segment), Px(TOOLBAR_HEIGHT)};
			const auto ProgressBrush = CreateSolidBrush(ACCENT); FillRect(Dc, &Progress, ProgressBrush); DeleteObject(ProgressBrush);
		}
		const auto FrameBrush = CreateSolidBrush(BORDER); FrameRect(Dc, &Rect, FrameBrush); DeleteObject(FrameBrush);
	}

	void PaintButton(const DRAWITEMSTRUCT &Item)
	{
		const int Id = Item.CtlID;
		const bool Hover = m_Hovered == Item.hwndItem;
		const bool Down = (Item.itemState & ODS_SELECTED) != 0;
		const bool Disabled = (Item.itemState & ODS_DISABLED) != 0;
		const bool Primary = Id == 104;
		const bool Close = Id == 107;
		const bool GameControl = Id == 108;
		const auto Brush = CreateSolidBrush(SURFACE); FillRect(Item.hDC, &Item.rcItem, Brush); DeleteObject(Brush);
		RECT Rect = Item.rcItem; InflateRect(&Rect, -Px(1), -Px(1));
		const COLORREF Background = Primary ? (Down ? RGB(69, 167, 192) : Hover ? RGB(127, 227, 245) : ACCENT) :
			GameControl && m_GameControlEnabled ? (Hover ? RGB(89, 226, 190) : RGB(49, 151, 125)) :
			Close && Hover ? RGB(164, 61, 74) : Down ? RGB(49, 67, 86) : Hover ? RGB(37, 49, 67) : SURFACE;
		FillRounded(Item.hDC, Rect, Background, Px(12), Primary ? Background : Hover ? BORDER : SURFACE);
		const COLORREF Foreground = Disabled ? RGB(66, 80, 100) : (Primary || (GameControl && m_GameControlEnabled)) ? RGB(13, 35, 48) : TEXT;
		if(Id == 104 || Id == 105 || GameControl)
		{
			const wchar_t *pLabel = Id == 104 ? L"前往  →" : Id == 105 ? L"返回游戏" : m_GameControlEnabled ? L"游戏操控  开" : L"游戏操控  关";
			DrawTextLabel(Item.hDC, m_Font, Foreground, Rect, pLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
			if(pSelf->m_Dragging)
			{
				POINT Cursor{}; GetCursorPos(&Cursor);
				POINT Position{pSelf->m_WindowOrigin.x + Cursor.x - pSelf->m_DragOrigin.x, pSelf->m_WindowOrigin.y + Cursor.y - pSelf->m_DragOrigin.y};
				MapWindowPoints(nullptr, pSelf->m_Parent, &Position, 1);
				SetWindowPos(Window, nullptr, Position.x, Position.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
				return 0;
			}
			break;
		case WM_LBUTTONUP:
			if(pSelf->m_Dragging) { pSelf->m_Dragging = false; ReleaseCapture(); return 0; }
			break;
		case WM_CAPTURECHANGED:
			pSelf->m_Dragging = false; break;
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
			SetTextColor(reinterpret_cast<HDC>(WParam), TEXT);
			SetBkColor(reinterpret_cast<HDC>(WParam), ADDRESS);
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
		case WM_SIZE:
			pSelf->Layout();
			InvalidateRect(Window, nullptr, FALSE);
			return 0;
		case WM_COMMAND:
			if(LOWORD(WParam) == 104) pSelf->Navigate();
			else if(LOWORD(WParam) == 105 || LOWORD(WParam) == 107) pSelf->Hide();
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
		for(int i = 0; i < 3; ++i) if(m_aButtons[i]) MoveWindow(m_aButtons[i], Px(12 + i * 39), Px(52), Px(34), Px(36), TRUE);
		const RECT AddressBox = AddressRect();
		if(m_Address) MoveWindow(m_Address, AddressBox.left + Px(48), AddressBox.top + Px(8), std::max(Px(20), static_cast<int>(AddressBox.right - AddressBox.left) - Px(60)), Px(22), TRUE);
		if(m_aButtons[3]) MoveWindow(m_aButtons[3], Rect.right - Px(92), Px(52), Px(78), Px(36), TRUE);
		if(m_aButtons[4]) MoveWindow(m_aButtons[4], Rect.right - Px(180), Px(7), Px(84), Px(28), TRUE);
		if(m_aButtons[5]) MoveWindow(m_aButtons[5], Rect.right - Px(86), Px(7), Px(32), Px(28), TRUE);
		if(m_aButtons[6]) MoveWindow(m_aButtons[6], Rect.right - Px(46), Px(7), Px(32), Px(28), TRUE);
		if(m_aButtons[7]) MoveWindow(m_aButtons[7], Rect.right - Px(338), Px(7), Px(145), Px(28), TRUE);
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
		m_pController->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
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
	std::string m_Error;

	void Hide()
	{
		m_Visible = false;
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
		m_AddressBrush = CreateSolidBrush(ADDRESS);
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

	void Update()
	{
		if(!m_Visible || !m_Window || !IsWindow(m_Window) || !m_Parent || !IsWindow(m_Parent)) return;
		if(IsIconic(m_Parent))
		{
			SetGameControl(false);
			return;
		}
		if(IsIconic(m_Window)) return;
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
		if(m_pController) m_pController->NotifyParentWindowPositionChanged();
	}

	void Shutdown()
	{
		m_Stopped = true;
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
	m_pImpl->Update();
	const bool Failed = !m_pImpl->m_Error.empty();
	if(Failed)
	{
		GameClient()->m_Chat.Echo(m_pImpl->m_Error.c_str());
		Client()->AddWarning(SWarning("内置浏览器", m_pImpl->m_Error.c_str()));
		m_pImpl->Shutdown();
	}
	if(m_pImpl->m_Visible) { Input()->MouseModeAbsolute(); Graphics()->SetWindowGrab(false); }
	else if(m_WasVisible || m_RestoreFullscreen || Failed)
	{
		if(m_RestoreFullscreen && g_Config.m_GfxFullscreen == 2) { g_Config.m_GfxFullscreen = 1; Graphics()->SetWindowParams(1, g_Config.m_GfxBorderless); }
		m_RestoreFullscreen = false;
		// Both the game and its menus use SDL relative motion for the cursor.
		Input()->MouseModeRelative();
	}
	m_WasVisible = m_pImpl->m_Visible;
	if(Failed) m_pImpl.reset();
}

void CWebBrowser::OnShutdown()
{
	if(m_pImpl) { m_pImpl->Shutdown(); m_pImpl.reset(); }
	if(m_RestoreFullscreen && g_Config.m_GfxFullscreen == 2) g_Config.m_GfxFullscreen = 1;
	m_RestoreFullscreen = false;
}
