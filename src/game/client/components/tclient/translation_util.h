#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TRANSLATION_UTIL_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TRANSLATION_UTIL_H
#include <string>
#include <string_view>
namespace OutgoingTranslation
{
struct SCommand { bool m_Handled = false; std::string m_Target, m_Text, m_Error; };
SCommand Parse(std::string_view Text);
std::string Sanitize(std::string_view Text);
}
#include <engine/shared/json.h>
namespace MyMemory
{
bool ShouldTranslate(const char *pText);
bool ShouldTranslatePlayerMessage(int ClientId, const char *pText);
std::string RequestUrl(const std::string &Text, const std::string &Source = "autodetect", const std::string &Target = "zh-CN");
struct SResponse
{
	std::string m_Text, m_Source, m_Error;
	bool m_QuotaFinished = false;
};
SResponse ParseResponse(const json_value *pJson);
}
#endif
