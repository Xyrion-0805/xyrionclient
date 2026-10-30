#ifndef ENGINE_CLIENT_DEMO_PROJECT_EXPORT_H
#define ENGINE_CLIENT_DEMO_PROJECT_EXPORT_H

#include <engine/shared/demo_project.h>
#include <engine/shared/jobs.h>
#include <engine/shared/snapshot.h>

#include <atomic>
#include <string>

class IStorage;

class CDemoProjectExport : public IJob
{
	CSnapshotDelta m_Delta, m_DeltaSixup;
	IStorage *m_pStorage;
	std::string m_Source, m_Folder;
	std::vector<demo_edit::SClip> m_vClips;
	std::atomic<bool> m_Cancel{false};
	std::atomic<int> m_Completed{0};
	bool m_RemoveChat, m_Success = false;
	std::string m_Error;
	void Run() override;
	static bool Filter(const void *pData, int Size, void *pUser);

public:
	CDemoProjectExport(const CSnapshotDelta &Delta, const CSnapshotDelta &DeltaSixup, IStorage *pStorage,
		std::string Source, std::string Folder, std::vector<demo_edit::SClip> Clips, bool RemoveChat);
	void Cancel() { m_Cancel.store(true); }
	int Completed() const { return m_Completed.load(); }
	bool Success() const { return m_Success; } // Read after State() == STATE_DONE.
	const std::string &Error() const { return m_Error; }
	const std::string &Folder() const { return m_Folder; }
};
#endif
