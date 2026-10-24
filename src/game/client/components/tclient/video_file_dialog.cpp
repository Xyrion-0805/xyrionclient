#include "video_file_dialog.h"
#include <base/detect.h>
#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>
#undef NOGDI
#include <windows.h>
#include <wingdi.h>
#include <commdlg.h>
#include <SDL.h>
#include <SDL_syswm.h>
#endif

std::optional<std::string> SelectVideoFile(const char *pCurrent)
{
#if defined(CONF_FAMILY_WINDOWS)
	HMODULE Library = LoadLibraryW(L"comdlg32.dll");
	if(!Library) return std::nullopt;
	using FOpen = BOOL(WINAPI *)(LPOPENFILENAMEW);
	auto Open = reinterpret_cast<FOpen>(GetProcAddress(Library, "GetOpenFileNameW"));
	wchar_t aPath[32768] = {};
	const auto Current = windows_utf8_to_wide(pCurrent);
	if(Current.size() < std::size(aPath)) std::copy(Current.begin(), Current.end(), aPath);
	OPENFILENAMEW Dialog{};
	Dialog.lStructSize = sizeof(Dialog);
	SDL_SysWMinfo Info{};
	SDL_VERSION(&Info.version);
	if(auto *pWindow = SDL_GetKeyboardFocus(); pWindow && SDL_GetWindowWMInfo(pWindow, &Info)) Dialog.hwndOwner = Info.info.win.window;
	Dialog.lpstrFile = aPath;
	Dialog.nMaxFile = std::size(aPath);
	Dialog.lpstrTitle = L"选择视频背景";
	Dialog.lpstrFilter = L"视频文件 (*.mp4;*.mkv;*.webm;*.mov;*.avi)\0*.mp4;*.mkv;*.webm;*.mov;*.avi\0所有文件\0*.*\0\0";
	Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
	const bool Selected = Open && Open(&Dialog);
	FreeLibrary(Library);
	if(Selected) return windows_wide_to_utf8(aPath);
#endif
	return std::nullopt;
}
