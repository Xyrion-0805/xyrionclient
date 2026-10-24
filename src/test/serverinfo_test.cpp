#include <engine/external/json-parser/json.h>
#include <engine/serverbrowser.h>
#include <engine/shared/serverinfo.h>
#include <engine/shared/json.h>

#include <gtest/gtest.h>

TEST(ServerInfo, ParseLocation)
{
	int Result;
	EXPECT_TRUE(CServerInfo::ParseLocation(&Result, "xx"));
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "an"));
	EXPECT_EQ(Result, CServerInfo::LOC_UNKNOWN);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "af"));
	EXPECT_EQ(Result, CServerInfo::LOC_AFRICA);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "eu-n"));
	EXPECT_EQ(Result, CServerInfo::LOC_EUROPE);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "na"));
	EXPECT_EQ(Result, CServerInfo::LOC_NORTH_AMERICA);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "sa"));
	EXPECT_EQ(Result, CServerInfo::LOC_SOUTH_AMERICA);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "as:e"));
	EXPECT_EQ(Result, CServerInfo::LOC_ASIA);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "as:cn"));
	EXPECT_EQ(Result, CServerInfo::LOC_CHINA);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "oc"));
	EXPECT_EQ(Result, CServerInfo::LOC_AUSTRALIA);
}

static unsigned int ParseCrcOrDeadbeef(const char *pString)
{
	unsigned int Result;
	if(ParseCrc(&Result, pString))
	{
		Result = 0xdeadbeef;
	}
	return Result;
}

TEST(ServerInfo, Crc)
{
	EXPECT_EQ(ParseCrcOrDeadbeef("00000000"), 0);
	EXPECT_EQ(ParseCrcOrDeadbeef("00000001"), 1);
	EXPECT_EQ(ParseCrcOrDeadbeef("12345678"), 0x12345678);
	EXPECT_EQ(ParseCrcOrDeadbeef("9abcdef0"), 0x9abcdef0);

	EXPECT_EQ(ParseCrcOrDeadbeef(""), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("a"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("x"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("ç"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("😢"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("0"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("000000000"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("00000000x"), 0xdeadbeef);
}

TEST(ServerInfo, MapPreviewMetadata)
{
	const char *pText = R"({"max_clients":64,"max_players":64,"passworded":false,"game_type":"DDRace","name":"Local","map":{"name":"test","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","size":2048,"url":"https://example.com/test.map"},"version":"1","clients":[]})";
	auto *pJson = JsonParse(pText, str_length(pText));
	CServerInfo2 Info{};
	ASSERT_FALSE(CServerInfo2::FromJson(&Info, pJson));
	json_value_free(pJson);
	const CServerInfo Converted = Info;
	EXPECT_TRUE(Converted.m_HasMapSha256);
	EXPECT_EQ(Converted.m_MapSize, 2048);
	EXPECT_STREQ(Converted.m_aMapUrl, "https://example.com/test.map");
	Info.m_MapSize = 4096;
	CServerInfo2 Other = Info;
	Other.m_MapSize = 2048;
	EXPECT_NE(Info, Other);
	pText = R"({"max_clients":64,"max_players":64,"passworded":false,"game_type":"DDRace","name":"Local","map":{"name":"test","sha256":"invalid","size":999999999,"url":"http://example.com/test.map"},"version":"1","clients":[]})";
	pJson = JsonParse(pText, str_length(pText));
	ASSERT_FALSE(CServerInfo2::FromJson(&Info, pJson));
	json_value_free(pJson);
	EXPECT_FALSE(Info.m_HasMapSha256);
	EXPECT_EQ(Info.m_MapSize, 0);
	EXPECT_STREQ(Info.m_aMapUrl, "");
}
