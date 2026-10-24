#include "test.h"

#include <base/detect.h>
#include <base/fs.h>
#include <engine/storage.h>
#include <game/client/components/tclient/modsystem/file_io.h>
#include <game/client/components/tclient/modsystem/mod.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>
#include <windows.h>
#endif

namespace
{
namespace ms = modsystem;
ms::CMod FileTestMod()
{
	ms::CMod Mod;
	Mod.m_Name = "中文 保存测试";
	Mod.m_vRules.emplace_back();
	ms::CScriptNode If;
	If.m_Kind = ms::CScriptNode::K_IF_ELSE;
	If.m_HasElse = true;
	ms::CScriptNode Echo;
	Echo.m_Block.m_Action = ms::ACT_ECHO;
	Echo.m_Block.m_Argument = "你好，积木！";
	If.m_vThen.push_back(Echo);
	Mod.m_vRules.front().m_vBody.push_back(If);
	Mod.m_vRules.front().m_vBody.push_back(Echo);
	return Mod;
}
std::string Read(IStorage &Storage, const std::string &Path, int Type)
{
	void *pData = nullptr;
	unsigned Size = 0;
	if(!Storage.ReadFile(Path.c_str(), Type, &pData, &Size))
	{
		ADD_FAILURE() << "Cannot read test file";
		return {};
	}
	std::string Result(static_cast<char *>(pData), Size);
	free(pData);
	return Result;
}
std::vector<std::string> Entries(IStorage &Storage, const char *pFolder)
{
	std::vector<std::string> Result;
	Storage.ListDirectory(IStorage::TYPE_SAVE, pFolder, [](const char *pName, int, int, void *pUser) {
		if(std::string(pName) != "." && std::string(pName) != "..")
			static_cast<std::vector<std::string> *>(pUser)->emplace_back(pName);
		return 0;
	}, &Result);
	return Result;
}
std::string AbsolutePath(IStorage &Storage, const char *pFilename)
{
	char aPath[IO_MAX_PATH_LENGTH];
	Storage.GetCompletePath(IStorage::TYPE_SAVE, pFilename, aPath, sizeof(aPath));
	if(!fs_is_relative_path(aPath))
		return aPath;
	char aWorkingDirectory[IO_MAX_PATH_LENGTH];
	EXPECT_TRUE(fs_getcwd(aWorkingDirectory, sizeof(aWorkingDirectory)));
	return std::string(aWorkingDirectory) + "/" + aPath;
}
}

TEST(ModSystemFiles, SaveOverwriteAndReloadChineseTree)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("mods", IStorage::TYPE_SAVE));
	auto Mod = FileTestMod();
	std::string Error, Path = "mods/中文 模组.mod";
	for(const auto &Text : {"第一次", "保存覆盖后"})
	{
		Mod.m_vRules.front().m_vBody.back().m_Block.m_Argument = Text;
		const auto Bytes = ms::SaveMod(Mod);
		ASSERT_TRUE(ms::WriteModFileAtomic(*pStorage, Path, false, Bytes, Error)) << Error;
		EXPECT_TRUE(Error.empty());
		const auto Saved = Read(*pStorage, Path, IStorage::TYPE_SAVE);
		EXPECT_EQ(Saved, Bytes);
		ms::CMod Reloaded;
		ASSERT_TRUE(ms::LoadMod(Saved, Reloaded, Error)) << Error;
		EXPECT_EQ(Reloaded.m_Name, Mod.m_Name);
		ASSERT_EQ(Reloaded.m_vRules.front().m_vBody.size(), 2U);
		EXPECT_EQ(Reloaded.m_vRules.front().m_vBody.back().m_Block.m_Argument, Text);
		EXPECT_EQ(Entries(*pStorage, "mods"), std::vector<std::string>{"中文 模组.mod"});
	}
}

TEST(ModSystemFiles, AbsoluteAndRelativeExportsOverwrite)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const auto Path = AbsolutePath(*pStorage, "中文 导出.mod");
	std::string Error;
	for(const auto &Name : {"第一次导出", "覆盖导出"})
	{
		auto Mod = FileTestMod();
		Mod.m_Name = Name;
		const auto Bytes = ms::SaveMod(Mod);
		ASSERT_TRUE(ms::WriteModFileAtomic(*pStorage, Path, true, Bytes, Error)) << Error;
		EXPECT_EQ(Read(*pStorage, Path, IStorage::TYPE_ABSOLUTE), Bytes);
	}
	const auto Bytes = ms::SaveMod(FileTestMod());
	ASSERT_TRUE(ms::WriteModFileAtomic(*pStorage, "relative.mod", true, Bytes, Error)) << Error;
	EXPECT_EQ(Read(*pStorage, "relative.mod", IStorage::TYPE_SAVE), Bytes);
	EXPECT_EQ(Entries(*pStorage, "").size(), 2U);
}

TEST(ModSystemFiles, FailedReplacementKeepsDestinationAndCleansTemporary)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	ASSERT_TRUE(pStorage->CreateFolder("existing.mod", IStorage::TYPE_SAVE));
	std::string Error;
	ASSERT_TRUE(ms::WriteModFileAtomic(*pStorage, "existing.mod/sentinel", false, "existing data", Error));
	EXPECT_FALSE(ms::WriteModFileAtomic(*pStorage, "existing.mod", false, "replacement", Error));
	EXPECT_FALSE(Error.empty());
	EXPECT_EQ(Read(*pStorage, "existing.mod/sentinel", IStorage::TYPE_SAVE), "existing data");
	EXPECT_EQ(Entries(*pStorage, ""), std::vector<std::string>{"existing.mod"});
	EXPECT_FALSE(ms::WriteModFileAtomic(*pStorage, "missing/child.mod", false, "new data", Error));
}

TEST(ModSystemFiles, InvalidPathAndOversizeDoNotChangeExistingFile)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	std::string Error;
	ASSERT_TRUE(ms::WriteModFileAtomic(*pStorage, "original.mod", false, "original data", Error));
	EXPECT_FALSE(ms::WriteModFileAtomic(*pStorage, "original.mod", false, std::string(1024 * 1024 + 1, 'x'), Error));
	EXPECT_FALSE(ms::WriteModFileAtomic(*pStorage, "", true, "new data", Error));
	EXPECT_FALSE(ms::WriteModFileAtomic(*pStorage, std::string("invalid\xff"), true, "new data", Error));
	EXPECT_EQ(Read(*pStorage, "original.mod", IStorage::TYPE_SAVE), "original data");
	EXPECT_EQ(Entries(*pStorage, ""), std::vector<std::string>{"original.mod"});
}

#if defined(CONF_FAMILY_WINDOWS)
TEST(ModSystemFiles, LockedDestinationKeepsOriginalBytes)
{
	CTestInfo Info;
	Info.m_DeleteTestStorageFilesOnSuccess = true;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	std::string Error;
	ASSERT_TRUE(ms::WriteModFileAtomic(*pStorage, "locked.mod", false, "original data", Error));
	const auto Path = AbsolutePath(*pStorage, "locked.mod");
	const auto Wide = windows_utf8_to_wide(Path.c_str());
	HANDLE File = CreateFileW(Wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(File, INVALID_HANDLE_VALUE);
	const bool Saved = ms::WriteModFileAtomic(*pStorage, Path, true, "replacement", Error);
	CloseHandle(File);
	EXPECT_FALSE(Saved);
	EXPECT_FALSE(Error.empty());
	EXPECT_EQ(Read(*pStorage, "locked.mod", IStorage::TYPE_SAVE), "original data");
	EXPECT_EQ(Entries(*pStorage, ""), std::vector<std::string>{"locked.mod"});
}
#endif
