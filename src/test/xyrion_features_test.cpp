#include <game/client/components/bestclient/asset_editor_util.h>
#include <engine/image.h>
#include <engine/gfx/image_loader.h>
#include <base/mem.h>
#include <base/str.h>
#include <game/client/components/tclient/modsystem/mod.h>
#include <game/client/components/tclient/translation_util.h>
#include <engine/shared/json.h>
#include <gtest/gtest.h>
#include <limits>

TEST(XyrionMods, CopiesGetIndependentIdsAndRespectLimits)
{
	namespace ms = modsystem;
	ms::CMod Mod; ms::CRule Rule; Rule.m_vConditions.emplace_back();
	ms::CScriptNode Node; Node.m_Kind = ms::CScriptNode::K_IF_ELSE; Node.m_HasElse = true; Node.m_vIfConds.emplace_back();
	Node.m_vThen.emplace_back(); Node.m_vElse.emplace_back(); Rule.m_vBody.push_back(Node); Mod.m_vRules.push_back(Rule);
	std::string Error; ms::TId Copy = 0;
	ASSERT_TRUE(ms::DuplicateRule(Mod, Rule.m_Id, Copy, Error)) << Error;
	ASSERT_TRUE(ms::ValidateMod(Mod, Error)) << Error;
	EXPECT_NE(Copy, Rule.m_Id); EXPECT_NE(Mod.m_vRules[1].m_vBody[0].m_vElse[0].m_Id, Node.m_vElse[0].m_Id);
	ASSERT_TRUE(ms::DuplicateNode(Mod, Node.m_Id, Error)) << Error; ASSERT_TRUE(ms::ValidateMod(Mod, Error)) << Error;
	EXPECT_EQ(Mod.m_vRules[0].m_vBody.size(), 2u);
	while(Mod.m_vRules.size() < ms::MAX_RULES) Mod.m_vRules.emplace_back();
	const std::string Before = ms::SaveMod(Mod);
	EXPECT_FALSE(ms::DuplicateRule(Mod, Rule.m_Id, Copy, Error)); EXPECT_EQ(ms::SaveMod(Mod), Before);
}
TEST(XyrionTranslation, OnlyNonChineseTextAndSafeUrl)
{
	EXPECT_FALSE(MyMemory::ShouldTranslate("你好，世界"));
	EXPECT_FALSE(MyMemory::ShouldTranslate("繁體中文"));
	EXPECT_FALSE(MyMemory::ShouldTranslate("hello 你好"));
	EXPECT_FALSE(MyMemory::ShouldTranslate("123 !!!"));
	EXPECT_FALSE(MyMemory::ShouldTranslate("/team 1"));
	EXPECT_FALSE(MyMemory::ShouldTranslate("https://example.com"));
	EXPECT_FALSE(MyMemory::ShouldTranslate("\xff"));
	EXPECT_TRUE(MyMemory::ShouldTranslate("Hello world"));
	EXPECT_TRUE(MyMemory::ShouldTranslate("Guten Morgen"));
	EXPECT_TRUE(MyMemory::ShouldTranslate("Привет"));
	EXPECT_TRUE(MyMemory::ShouldTranslate("今日はいい天気です"));
	EXPECT_TRUE(MyMemory::ShouldTranslate("안녕하세요"));
	EXPECT_EQ(MyMemory::RequestUrl("a&b \"?", "en"), "https://api.mymemory.translated.net/get?langpair=en%7Czh-CN&q=a%26b%20%22%3F");
	EXPECT_TRUE(MyMemory::RequestUrl("hello", "en&q=bad").empty());
	EXPECT_TRUE(MyMemory::RequestUrl(std::string(400, '&')).empty());
}
TEST(XyrionTranslation, ResponseAndQuotaErrors)
{
	const char *pText = R"({"responseStatus":200,"quotaFinished":false,"responseData":{"translatedText":"你好 &amp; 再见","detectedLanguage":"en"}})";
	auto *pJson = JsonParse(pText, str_length(pText));
	auto Result = MyMemory::ParseResponse(pJson); json_value_free(pJson);
	EXPECT_EQ(Result.m_Text, "你好 & 再见"); EXPECT_EQ(Result.m_Source, "en"); EXPECT_TRUE(Result.m_Error.empty());
	pText = R"({"responseStatus":429,"quotaFinished":true})";
	pJson = JsonParse(pText, str_length(pText)); Result = MyMemory::ParseResponse(pJson); json_value_free(pJson);
	EXPECT_TRUE(Result.m_QuotaFinished); EXPECT_FALSE(Result.m_Error.empty()); EXPECT_TRUE(Result.m_Text.empty());
	EXPECT_FALSE(MyMemory::ParseResponse(nullptr).m_Error.empty());
}

TEST(XyrionTranslation, ExplicitOutgoingCommand)
{
	using namespace OutgoingTranslation;
	for(const char *pTarget : {"en", "ru", "ko", "ja"})
	{
		const std::string Url = MyMemory::RequestUrl("你好 &?", "zh-CN", pTarget);
		EXPECT_EQ(Url, std::string("https://api.mymemory.translated.net/get?langpair=zh-CN%7C") + pTarget + "&q=%E4%BD%A0%E5%A5%BD%20%26%3F");
	}
	EXPECT_TRUE(MyMemory::RequestUrl("test", "zh-CN", "en&q=bad").empty());
	EXPECT_FALSE(Parse("/fyi en hi").m_Handled);
	EXPECT_FALSE(Parse("hello").m_Handled);
	for(const char *pCode : {"En", "Ru", "Ko", "Ja", "en", "RU"})
	{
		const auto Command = Parse(std::string("/fy ") + pCode + "  你好  世界");
		EXPECT_TRUE(Command.m_Handled); EXPECT_TRUE(Command.m_Error.empty());
		EXPECT_EQ(Command.m_Text, "你好  世界");
	}
	EXPECT_FALSE(Parse("/fy").m_Error.empty());
	EXPECT_FALSE(Parse("/fy En  ").m_Error.empty());
	EXPECT_FALSE(Parse("/fy xx 内容").m_Error.empty());
	EXPECT_EQ(Sanitize(" /kill\n"), "[翻译] /kill");
	EXPECT_EQ(Sanitize(" \n "), "");
	EXPECT_EQ(Sanitize("\xff"), "");
	const auto Long = Sanitize(std::string(254, 'a') + "你");
	EXPECT_LE(Long.size(), 255u); EXPECT_TRUE(str_utf8_check(Long.c_str()));
}

TEST(XyrionAssets, SafeExportNames)
{
	for(const char *pName : {"my_asset", "武器资源", "test.1"}) EXPECT_TRUE(AssetsEditor::ValidExportName(pName)) << pName;
	for(const char *pName : {"", ".", "..", "../game", "a/b", "a\\b", "default", "DEFAULT", "CON", "nul.png", "LPT1", "test.", "test ", " test", "a:b", "a\n", "\xff"})
		EXPECT_FALSE(AssetsEditor::ValidExportName(pName)) << pName;
	EXPECT_FALSE(AssetsEditor::ValidExportName(std::string(50, 'a').c_str()));
}

TEST(XyrionAssets, ScalingBoundsAlphaAndPngRoundTrip)
{
	CImageInfo Source, Target;
	Source.m_Width = 2; Source.m_Height = 1; Source.m_Format = CImageInfo::FORMAT_RGBA; Source.AllocateFillZero();
	const unsigned char aPixels[] = {255, 0, 0, 127, 0, 255, 0, 0};
	mem_copy(Source.m_pData, aPixels, sizeof(aPixels));
	Target.m_Width = 4; Target.m_Height = 2; Target.m_Format = CImageInfo::FORMAT_RGBA; Target.AllocateFillZero();
	EXPECT_TRUE(AssetsEditor::CopyScaled(Target, Source, 0, 0, 4, 2, 0, 0, 2, 1));
	EXPECT_EQ(Target.m_pData[3], 127); EXPECT_EQ(Target.m_pData[15], 0);
	EXPECT_EQ(Target.m_pData[16], 255); EXPECT_EQ(Target.m_pData[25], 255);
	auto Before = Target.DeepCopy();
	EXPECT_FALSE(AssetsEditor::CopyScaled(Target, Source, 1, 0, std::numeric_limits<int>::max(), 1, 0, 0, 1, 1));
	EXPECT_FALSE(AssetsEditor::CopyScaled(Target, Source, -1, 0, 1, 1, 0, 0, 1, 1));
	EXPECT_TRUE(Target.DataEquals(Before));
	CByteBufferWriter Writer;
	EXPECT_TRUE(CImageLoader::SavePng(Writer, Target));
	CByteBufferReader Reader(Writer.Data(), Writer.Size()); CImageInfo Decoded; int Incompatible = 0;
	EXPECT_TRUE(CImageLoader::LoadPng(Reader, "xyrion-test", Decoded, Incompatible));
	EXPECT_TRUE(Target.DataEquals(Decoded));
	Source.Free(); Target.Free(); Decoded.Free();
	Before.Free();
}

TEST(XyrionAssets, OverlappingCopyKeepsOriginalSamples)
{
	CImageInfo Image; Image.m_Width = 3; Image.m_Height = 1; Image.m_Format = CImageInfo::FORMAT_RGBA; Image.AllocateFillZero();
	Image.m_pData[0] = 1; Image.m_pData[4] = 2; Image.m_pData[8] = 3;
	EXPECT_TRUE(AssetsEditor::CopyScaled(Image, Image, 1, 0, 2, 1, 0, 0, 2, 1));
	EXPECT_EQ(Image.m_pData[4], 1); EXPECT_EQ(Image.m_pData[8], 2);
	Image.Free();
}

TEST(XyrionTranslation, SkipsServerAndSystemMessages)
{
	EXPECT_FALSE(MyMemory::ShouldTranslatePlayerMessage(-1, "EnglishPlayer entered the game"));
	EXPECT_FALSE(MyMemory::ShouldTranslatePlayerMessage(-2, "Server notice"));
	EXPECT_TRUE(MyMemory::ShouldTranslatePlayerMessage(3, "Hello world"));
	EXPECT_FALSE(MyMemory::ShouldTranslatePlayerMessage(3, "你好"));
	EXPECT_FALSE(MyMemory::ShouldTranslatePlayerMessage(3, "/help"));
}
