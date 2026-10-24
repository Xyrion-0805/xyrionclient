#include "translation_util.h"
#include <base/str.h>
#include <engine/shared/json.h>
#include <algorithm>

OutgoingTranslation::SCommand OutgoingTranslation::Parse(std::string_view Text)
{
	SCommand Result;
	if(Text.size() < 3 || Text.substr(0, 3) != "/fy" || (Text.size() > 3 && Text[3] != ' ' && Text[3] != '\t')) return Result;
	Result.m_Handled = true;
	Text.remove_prefix(3);
	const auto Trim = [](std::string_view &Value) { while(!Value.empty() && (Value.front() == ' ' || Value.front() == '\t')) Value.remove_prefix(1); };
	Trim(Text);
	const size_t End = Text.find_first_of(" \t");
	std::string Language(Text.substr(0, End));
	for(char &C : Language) if(C >= 'A' && C <= 'Z') C += 'a' - 'A';
	if(Language.empty()) Result.m_Error = "用法：/fy En 你要发送的文字";
	else if(Language != "en" && Language != "ru" && Language != "ko" && Language != "ja") Result.m_Error = "可用语言：En（英语）、Ru（俄语）、Ko（韩语）、Ja（日语）";
	else if(End == std::string_view::npos) Result.m_Error = "用法：/fy En 你要发送的文字";
	else
	{
		Text.remove_prefix(End); Trim(Text);
		if(Text.empty()) Result.m_Error = "用法：/fy En 你要发送的文字";
		else { Result.m_Target = Language; Result.m_Text = Text; }
	}
	return Result;
}

std::string OutgoingTranslation::Sanitize(std::string_view Text)
{
	std::string Value(Text);
	if(!str_utf8_check(Value.c_str())) return {};
	for(char &C : Value) if(static_cast<unsigned char>(C) < 32 || C == 127) C = ' ';
	const auto Start = Value.find_first_not_of(' ');
	if(Start == std::string::npos) return {};
	Value.erase(0, Start);
	Value.erase(Value.find_last_not_of(' ') + 1);
	if(Value.front() == '/') Value.insert(0, "[翻译] ");
	char aResult[256];
	str_copy(aResult, Value.c_str(), sizeof(aResult));
	str_utf8_fix_truncation(aResult);
	return aResult;
}

bool MyMemory::ShouldTranslate(const char *pText)
{
	if(!pText || !*pText || !str_utf8_check(pText) || *pText == '/' || str_startswith(pText, "https://") || str_startswith(pText, "http://")) return false;
	bool Han = false, Kana = false, Letters = false;
	for(const char *p = pText; *p;)
	{
		const int C = str_utf8_decode(&p);
		Han |= (C >= 0x3400 && C <= 0x9fff) || (C >= 0x20000 && C <= 0x323af) || (C >= 0xf900 && C <= 0xfaff);
		Kana |= C >= 0x3040 && C <= 0x30ff;
		Letters |= (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= 0xc0 && C <= 0x2af) || (C >= 0x370 && C <= 0x52f) || (C >= 0x590 && C <= 0x1fff) || (C >= 0xac00 && C <= 0xd7af);
	}
	// Kana distinguishes Japanese text that also contains Han characters.
	return Kana || (!Han && Letters);
}

bool MyMemory::ShouldTranslatePlayerMessage(int ClientId, const char *pText)
{
	return ClientId >= 0 && ShouldTranslate(pText);
}

std::string MyMemory::RequestUrl(const std::string &Text, const std::string &Source, const std::string &Target)
{
	// Source originates from the API. Never let it inject extra query fields.
	if(Source.empty() || Source.size() > 20 || !std::all_of(Source.begin(), Source.end(), [](unsigned char C) { return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '-'; })) return {};
	if(Target.empty() || Target.size() > 20 || !std::all_of(Target.begin(), Target.end(), [](unsigned char C) { return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '-'; })) return {};
	std::string Url = "https://api.mymemory.translated.net/get?langpair=" + Source + "%7C" + Target + "&q=";
	constexpr char aHex[] = "0123456789ABCDEF";
	for(unsigned char C : Text)
	{
		if((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-' || C == '_' || C == '.' || C == '~') Url += C;
		else { Url += '%'; Url += aHex[C >> 4]; Url += aHex[C & 15]; }
	}
	return Url.size() < 1024 ? Url : std::string{};
}

MyMemory::SResponse MyMemory::ParseResponse(const json_value *pJson)
{
	SResponse Result;
	if(!pJson || pJson->type != json_object) { Result.m_Error = "翻译接口返回格式错误"; return Result; }
	Result.m_QuotaFinished = json_boolean_get(json_object_get(pJson, "quotaFinished"));
	const auto *pStatus = json_object_get(pJson, "responseStatus");
	if(!pStatus || pStatus->type != json_integer || pStatus->u.integer != 200 || Result.m_QuotaFinished)
	{
		Result.m_Error = Result.m_QuotaFinished ? "翻译服务今日额度已用完" : "翻译服务暂不可用";
		return Result;
	}
	const auto *pData = json_object_get(pJson, "responseData");
	const char *pText = json_string_get(json_object_get(pData, "translatedText"));
	const char *pSource = json_string_get(json_object_get(pData, "detectedLanguage"));
	if(pSource) Result.m_Source = pSource;
	if(!pText || !*pText || !str_utf8_check(pText) || str_length(pText) >= 1024) { Result.m_Error = "翻译接口返回空文本或无效文本"; return Result; }
	Result.m_Text = pText;
	for(const auto &[Entity, Replacement] : {std::pair{"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}})
	{
		size_t Pos = 0;
		while((Pos = Result.m_Text.find(Entity, Pos)) != std::string::npos) { Result.m_Text.replace(Pos, str_length(Entity), Replacement); Pos += str_length(Replacement); }
	}
	Result.m_Text.erase(std::remove_if(Result.m_Text.begin(), Result.m_Text.end(), [](unsigned char C) { return C < 32 || C == 127; }), Result.m_Text.end());
	return Result;
}
