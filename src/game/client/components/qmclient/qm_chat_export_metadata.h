#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_EXPORT_METADATA_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_CHAT_EXPORT_METADATA_H

#include <cstddef>
#include <memory>
#include <string>

namespace QmChatAvatar
{
	struct SSnapshot;
}

namespace QmChatExport
{
	inline int ResolveSenderId(int ClientId, bool OutgoingWhisper, int SourceConnection, const int *pLocalIds, size_t NumLocalIds, int SnapshotLocalId, bool DemoPlayback)
	{
		if(!OutgoingWhisper)
			return ClientId;
		// 私聊协议携带收件人；发件人必须使用消息来源连接，录像则使用录制的本地身份。
		if(DemoPlayback)
			return SnapshotLocalId;
		if(SourceConnection >= 0 && (size_t)SourceConnection < NumLocalIds)
			return pLocalIds[SourceConnection];
		return SnapshotLocalId;
	}

	// 随发言保留身份和头像；导出时不再根据当前玩家名称反查皮肤。
	struct SMetadata
	{
		std::string m_Sender;
		std::string m_Message;
		bool m_Local = false;
		std::shared_ptr<const QmChatAvatar::SSnapshot> m_pAvatar;
	};
}

#endif
