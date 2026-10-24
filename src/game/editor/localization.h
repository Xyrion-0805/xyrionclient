#ifndef GAME_EDITOR_LOCALIZATION_H
#define GAME_EDITOR_LOCALIZATION_H
class IStorage;
class IConsole;
void InitEditorLocalization(IStorage *pStorage, IConsole *pConsole);
[[gnu::format_arg(1)]] const char *EditorLocalize(const char *pText);
#endif
