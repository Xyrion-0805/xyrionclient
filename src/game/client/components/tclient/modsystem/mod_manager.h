#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_MOD_MANAGER_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_MODSYSTEM_MOD_MANAGER_H
#include "runtime.h"
#include <game/client/component.h>

// Component name is global; the script data CMod lives in namespace modsystem.
class CModSystem : public CComponent, private modsystem::IRuntimeHost
{
	modsystem::CExecutor m_Executor;
	int m_LastTick = -1, m_LastDummy = -1, m_LastHook = 0;
	bool m_WasAlive = false, m_WasGround = false, m_WasFrozen = false;
	bool m_LastHookInput = false, m_LastJumpInput = false, m_WasEnabled = true;
	int64_t m_LastReload = 0;
	bool m_Dispatching = false;
	bool Read(const std::string &Path, bool Absolute, std::string &Text, std::string &Error);
	bool AtomicWrite(const std::string &Path, bool Absolute, const std::string &Text, std::string &Error);
	bool Grounded() const;
	double AimAngle() const;
	modsystem::SNearest Trace(modsystem::ENearestTarget Target, double Angle) const;
	void SetAngle(double Degrees);
	bool Condition(const modsystem::CCondition &Cond, const modsystem::CMod &Mod, modsystem::SRuntimeState &State) override;
	void Action(const modsystem::CBlock &Block, const modsystem::CMod &Mod, modsystem::SRuntimeState &State) override;
public:
	std::vector<modsystem::CMod> m_vMods;
	std::string m_LastError;
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnMapLoad() override;
	void OnUpdate() override;
	void OnNewSnapshot() override;
	void OnMessage(int Msg, void *pRawMsg) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void FireHook(modsystem::EHook Hook, std::string_view Argument = {});
	int AddMod(const std::string &Name);
	bool Save(int Index, std::string &Error);
	bool Rename(int Index, const std::string &Name, std::string &Error);
	bool DeleteMod(int Index, std::string &Error);
	bool Reload(int Index, bool DiscardDirty, std::string &Error);
	void ReloadAll(bool HotReload = true);
	int Import(const std::string &Path, std::string &Error);
	bool Export(int Index, const std::string &Path, std::string &Error);
	void OpenDirectory();
	void Changed(modsystem::CMod &Mod) { Mod.Touch(); m_Executor.Cancel(Mod.m_Id); }
	void SetEnabled(int Index, bool Enabled);
	const modsystem::SRuntimeState *State(modsystem::TId Id) const { return m_Executor.State(Id); }
	double LastValue(modsystem::TId Id) const { const auto *p = State(Id); return p ? p->m_Register : 0; }
	bool NearestValid(modsystem::TId Id) const { const auto *p = State(Id); return p && p->m_Nearest.m_Valid; }
};
#endif
