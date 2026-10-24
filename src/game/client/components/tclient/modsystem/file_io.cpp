#include "file_io.h"
#include "mod.h"

#include <base/detect.h>
#include <base/fs.h>
#include <base/io.h>
#include <base/process.h>
#include <base/str.h>
#include <engine/storage.h>

#include <cstdio>

#if defined(CONF_FAMILY_WINDOWS)
#include <base/windows.h>
#include <windows.h>
#endif

namespace modsystem
{
bool WriteModFileAtomic(IStorage &Storage, const std::string &Path, bool AllowAbsolute, const std::string &Text, std::string &Error)
{
	if(Path.empty() || Path.find('\0') != std::string::npos || !str_utf8_check(Path.c_str()) || Path.size() + 64 >= IO_MAX_PATH_LENGTH)
	{
		Error = "保存路径为空、过长或不是有效 UTF-8";
		return false;
	}
	if(Text.size() > 1024 * 1024)
	{
		Error = "模组文件超过 1 MiB，无法保存";
		return false;
	}
	const int Type = AllowAbsolute && !fs_is_relative_path(Path.c_str()) ? IStorage::TYPE_ABSOLUTE : IStorage::TYPE_SAVE;
	// Each process uses distinct temporary names in the destination directory.
	const std::string Tmp = Path + ".tmp." + std::to_string(process_id()) + "." + std::to_string(NewId());
	char aTemporary[IO_MAX_PATH_LENGTH];
	IOHANDLE File = Storage.OpenFile(Tmp.c_str(), IOFLAG_WRITE, Type, aTemporary, sizeof(aTemporary));
	if(!File)
	{
		Error = "无法创建临时文件: " + Tmp;
		return false;
	}
	bool Ok = io_write(File, Text.data(), static_cast<unsigned>(Text.size())) == Text.size();
	if(io_sync(File))
		Ok = false;
	if(io_close(File))
		Ok = false;
	if(Ok)
	{
		std::string Destination = Path;
		if(Type == IStorage::TYPE_SAVE)
		{
			char aDestination[IO_MAX_PATH_LENGTH];
			Storage.GetCompletePath(IStorage::TYPE_SAVE, Path.c_str(), aDestination, sizeof(aDestination));
			Destination = aDestination;
		}
		// IStorage::RenameFile and GetCompletePath do not accept TYPE_ABSOLUTE.
		// Replace the resolved paths directly, without deleting the old file on
		// failure. Both paths are in the same directory, so no copy is needed.
#if defined(CONF_FAMILY_WINDOWS)
		const auto TemporaryWide = windows_utf8_to_wide(aTemporary);
		const auto DestinationWide = windows_utf8_to_wide(Destination.c_str());
		Ok = MoveFileExW(TemporaryWide.c_str(), DestinationWide.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
		Ok = std::rename(aTemporary, Destination.c_str()) == 0;
#endif
	}
	if(!Ok)
	{
		Storage.RemoveFile(Tmp.c_str(), Type);
		Error = "保存失败，原文件保留: " + Path;
		return false;
	}
	Error.clear();
	return true;
}
}
