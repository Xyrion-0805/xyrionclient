#include "localization.h"
#include <base/str.h>
#include <engine/shared/config.h>
#include <game/localization.h>
namespace
{
	CLocalizationDatabase s_EditorChinese;
	CLocalizationDatabase s_EditorFallback;
}
void InitEditorLocalization(IStorage *pStorage, IConsole *pConsole)
{
	s_EditorChinese.Load("tclient/editor_zh.txt", pStorage, pConsole);
	s_EditorFallback.Load("languages/simplified_chinese.txt", pStorage, pConsole);
}
const char *EditorLocalize(const char *pText)
{
	if(!pText) return "";
	const char *pTranslation = s_EditorChinese.FindString(str_quickhash(pText), str_quickhash(""));
	if(!pTranslation)
		pTranslation = s_EditorFallback.FindString(str_quickhash(pText), str_quickhash(""));
	return pTranslation ? pTranslation : pText;
}
