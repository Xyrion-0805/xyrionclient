#include "demo_project_export.h"

#include <base/str.h>
#include <engine/shared/demo.h>
#include <engine/shared/packer.h>
#include <engine/storage.h>
#include <generated/protocol.h>
#include <generated/protocol7.h>

CDemoProjectExport::CDemoProjectExport(const CSnapshotDelta &Delta, const CSnapshotDelta &DeltaSixup, IStorage *pStorage,
	std::string Source, std::string Folder, std::vector<demo_edit::SClip> Clips, bool RemoveChat) :
	m_Delta(Delta), m_DeltaSixup(DeltaSixup), m_pStorage(pStorage), m_Source(std::move(Source)),
	m_Folder(std::move(Folder)), m_vClips(std::move(Clips)), m_RemoveChat(RemoveChat)
{
}

bool CDemoProjectExport::Filter(const void *pData, int Size, void *pUser)
{
	const auto *pSelf = static_cast<CDemoProjectExport *>(pUser);
	if(!pSelf->m_RemoveChat) return false;
	CUnpacker Unpacker;
	Unpacker.Reset(pData, Size);
	const int Message = Unpacker.GetInt();
	// Both 0.6 and 0.7 use message id 3 for Sv_Chat.
	static_assert(NETMSGTYPE_SV_CHAT == protocol7::NETMSGTYPE_SV_CHAT);
	return !Unpacker.Error() && !(Message & 1) && (Message >> 1) == NETMSGTYPE_SV_CHAT;
}

void CDemoProjectExport::Run()
{
	if(m_vClips.empty() || m_pStorage->FolderExists(m_Folder.c_str(), IStorage::TYPE_SAVE))
	{
		m_Error = "没有片段，或导出文件夹已经存在";
		return;
	}
	m_pStorage->CreateFolder("demos", IStorage::TYPE_SAVE);
	m_pStorage->CreateFolder("demos/edits", IStorage::TYPE_SAVE);
	if(!m_pStorage->CreateFolder(m_Folder.c_str(), IStorage::TYPE_SAVE))
	{
		m_Error = "无法创建 Demo 导出文件夹";
		return;
	}
	CDemoEditor Editor;
	Editor.Init(&m_Delta, &m_DeltaSixup, nullptr, m_pStorage);
	for(size_t I = 0; I < m_vClips.size(); ++I)
	{
		if(m_Cancel.load()) { m_Error = "已取消，已完成的片段保留"; return; }
		char aPath[IO_MAX_PATH_LENGTH], aTemp[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/clip-%02d.demo", m_Folder.c_str(), (int)I + 1);
		str_format(aTemp, sizeof(aTemp), "%s/clip-%02d.partial.demo", m_Folder.c_str(), (int)I + 1);
		const auto &Clip = m_vClips[I];
		if(!Editor.SliceWithCancel(m_Source.c_str(), aTemp, Clip.m_In, Clip.m_Out - 1, Filter, this, &m_Cancel))
		{
			m_pStorage->RemoveFile(aTemp, IStorage::TYPE_SAVE);
			m_Error = m_Cancel.load() ? "已取消，已完成的片段保留" : "Demo 剪辑失败：片段至少需要两个快照，请检查源文件和剩余磁盘空间";
			return;
		}
		if(!m_pStorage->RenameFile(aTemp, aPath, IStorage::TYPE_SAVE))
		{
			m_Error = "片段已生成但无法完成重命名";
			return;
		}
		m_Completed.store((int)I + 1);
	}
	m_Success = true;
}
