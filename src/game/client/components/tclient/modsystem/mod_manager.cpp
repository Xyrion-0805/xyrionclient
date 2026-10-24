#include "mod_manager.h"
#include "file_io.h"
#include <base/io.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/client.h>
#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <generated/client_data.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/mapitems.h>
#include <algorithm>
#include <cmath>
#include <set>

using namespace modsystem;
namespace
{
constexpr double PI = 3.14159265358979323846;
int ListMods(const char *pName, int IsDir, int Type, void *pUser)
{
	std::string Name(pName); if(!IsDir && Name.size() > 4 && Name.substr(Name.size() - 4) == ".mod") static_cast<std::vector<std::string> *>(pUser)->push_back("mods/" + Name); return 0;
}
}
bool CModSystem::Read(const std::string &Path, bool Absolute, std::string &Text, std::string &Error)
{
	IOHANDLE File = Storage()->OpenFile(Path.c_str(), IOFLAG_READ, Absolute ? IStorage::TYPE_SAVE_OR_ABSOLUTE : IStorage::TYPE_SAVE);
	if(!File) { Error = "无法打开文件: " + Path; return false; }
	int64_t Size = io_length(File); bool Ok = Size >= 0 && Size <= 1024 * 1024;
	if(Ok) { Text.resize(static_cast<size_t>(Size)); Ok = io_read(File, Text.data(), static_cast<unsigned>(Size)) == static_cast<unsigned>(Size); }
	if(io_close(File)) Ok = false;
	if(!Ok) Error = "文件读取失败或超过 1 MiB: " + Path;
	return Ok;
}
bool CModSystem::AtomicWrite(const std::string &Path, bool Absolute, const std::string &Text, std::string &Error)
{
	return WriteModFileAtomic(*Storage(), Path, Absolute, Text, Error);
}
int CModSystem::AddMod(const std::string &Name)
{
	for(size_t i = 0; i < m_vMods.size(); ++i) if(m_vMods[i].m_Name == Name) return static_cast<int>(i);
	modsystem::CMod Mod; Mod.m_Name = Name.empty() ? "新模组" : Name; Mod.m_vRules.emplace_back(); m_vMods.push_back(std::move(Mod)); return static_cast<int>(m_vMods.size() - 1);
}
bool CModSystem::Save(int Index, std::string &Error)
{
	if(Index < 0 || Index >= static_cast<int>(m_vMods.size())) return false;
	auto &M = m_vMods[Index]; if(!ValidateMod(M, Error)) return false;
	std::string Path = M.m_Filename.empty() ? "mods/" + SafeFilename(M.m_Name) + ".mod" : M.m_Filename;
	if(M.m_Filename.empty() && Storage()->FileExists(Path.c_str(), IStorage::TYPE_SAVE)) { Error = "同名文件已存在，请改名"; return false; }
	std::string Bytes = SaveMod(M); if(!AtomicWrite(Path, false, Bytes, Error)) return false;
	M.m_Filename = Path; M.m_DiskText = Bytes; M.m_Dirty = false; M.m_RemovedAimActions = false; return true;
}
bool CModSystem::Rename(int Index, const std::string &Name, std::string &Error)
{
	if(Index < 0 || Index >= static_cast<int>(m_vMods.size())) return false;
	for(size_t i = 0; i < m_vMods.size(); ++i) if(i != static_cast<size_t>(Index) && m_vMods[i].m_Name == Name) { Error = "模组名已存在"; return false; }
	auto &M = m_vMods[Index]; modsystem::CMod Candidate = M; Candidate.m_Name = Name;
	if(!ValidateMod(Candidate, Error)) return false;
	std::string Path = "mods/" + SafeFilename(Name) + ".mod";
	if(Path != M.m_Filename && Storage()->FileExists(Path.c_str(), IStorage::TYPE_SAVE)) { Error = "目标文件已存在"; return false; }
	std::string Bytes = SaveMod(Candidate); if(!AtomicWrite(Path, false, Bytes, Error)) return false;
	if(!M.m_Filename.empty() && M.m_Filename != Path && !Storage()->RemoveFile(M.m_Filename.c_str(), IStorage::TYPE_SAVE)) { Storage()->RemoveFile(Path.c_str(), IStorage::TYPE_SAVE); Error = "旧文件删除失败，改名已回滚"; return false; }
	m_Executor.Cancel(M.m_Id); Candidate.Touch(); Candidate.m_Filename = Path; Candidate.m_DiskText = Bytes; Candidate.m_Dirty = false; Candidate.m_RemovedAimActions = false; M = std::move(Candidate); return true;
}
bool CModSystem::DeleteMod(int Index, std::string &Error)
{
	if(Index < 0 || Index >= static_cast<int>(m_vMods.size())) return false;
	auto &M = m_vMods[Index]; if(!M.m_Filename.empty() && Storage()->FileExists(M.m_Filename.c_str(), IStorage::TYPE_SAVE) && !Storage()->RemoveFile(M.m_Filename.c_str(), IStorage::TYPE_SAVE)) { Error = "无法删除模组文件"; return false; }
	m_Executor.Cancel(M.m_Id); m_vMods.erase(m_vMods.begin() + Index); return true;
}
bool CModSystem::Reload(int Index, bool DiscardDirty, std::string &Error)
{
	if(Index < 0 || Index >= static_cast<int>(m_vMods.size())) return false;
	auto &M = m_vMods[Index]; if(M.m_Dirty && !DiscardDirty) { Error = "存在未保存修改；再次点击重载以确认丢弃"; return false; }
	if(M.m_Filename.empty()) { Error = "请先保存新模组"; return false; }
	std::string Bytes; if(!Read(M.m_Filename, false, Bytes, Error)) return false;
	modsystem::CMod New; New.m_Filename = M.m_Filename; if(!LoadMod(Bytes, New, Error)) return false;
	m_Executor.Cancel(M.m_Id); New.m_Id = M.m_Id; New.m_Revision = M.m_Revision + 1; New.m_DiskText = Bytes; M = std::move(New); return true;
}
void CModSystem::ReloadAll(bool HotReload)
{
	std::vector<std::string> Paths; Storage()->ListDirectory(IStorage::TYPE_SAVE, "mods", ListMods, &Paths); std::sort(Paths.begin(), Paths.end());
	for(const auto &Path : Paths)
	{
		auto It = std::find_if(m_vMods.begin(), m_vMods.end(), [&](const modsystem::CMod &M) { return M.m_Filename == Path; });
		std::string Bytes, Error; if(!Read(Path, false, Bytes, Error)) { m_LastError = Error; continue; }
		if(It != m_vMods.end() && Bytes == It->m_DiskText) continue;
		if(It != m_vMods.end() && It->m_Dirty) { m_LastError = "磁盘已修改「" + It->m_Name + "」，保留编辑中的内容"; continue; }
		modsystem::CMod M; M.m_Filename = Path; if(!LoadMod(Bytes, M, Error)) { m_LastError = Path + ": " + Error; continue; }
		M.m_DiskText = Bytes;
		if(It == m_vMods.end()) { auto Same = std::find_if(m_vMods.begin(), m_vMods.end(), [&](const modsystem::CMod &Existing) { return Existing.m_Name == M.m_Name; }); if(Same != m_vMods.end()) { m_LastError = "重复模组名: " + M.m_Name; continue; } m_vMods.push_back(std::move(M)); }
		else { m_Executor.Cancel(It->m_Id); M.m_Id = It->m_Id; M.m_Revision = It->m_Revision + 1; *It = std::move(M); }
	}
	for(size_t i = m_vMods.size(); i-- > 0;) { auto &M = m_vMods[i]; if(!M.m_Filename.empty() && std::find(Paths.begin(), Paths.end(), M.m_Filename) == Paths.end()) { if(M.m_Dirty) { m_LastError = "文件被移除，保留未保存模组: " + M.m_Name; continue; } m_Executor.Cancel(M.m_Id); m_vMods.erase(m_vMods.begin() + static_cast<ptrdiff_t>(i)); } }
	(void)HotReload;
}
int CModSystem::Import(const std::string &Path, std::string &Error)
{
	std::string Bytes; modsystem::CMod M; if(!Read(Path, true, Bytes, Error) || !LoadMod(Bytes, M, Error)) return -1;
	std::string Base = M.m_Name; int Suffix = 2; while(std::any_of(m_vMods.begin(), m_vMods.end(), [&](const modsystem::CMod &X) { return X.m_Name == M.m_Name; })) M.m_Name = Base + std::to_string(Suffix++);
	M.m_Filename.clear(); M.m_DiskText.clear(); M.m_Dirty = true; m_vMods.push_back(std::move(M)); return static_cast<int>(m_vMods.size() - 1);
}
bool CModSystem::Export(int Index, const std::string &Path, std::string &Error) { if(Index < 0 || Index >= static_cast<int>(m_vMods.size()) || Path.empty() || !ValidateMod(m_vMods[Index], Error)) return false; return AtomicWrite(Path, true, SaveMod(m_vMods[Index]), Error); }
void CModSystem::OpenDirectory() { char aPath[IO_MAX_PATH_LENGTH]; Storage()->GetCompletePath(IStorage::TYPE_SAVE, "mods", aPath, sizeof(aPath)); Client()->ViewFile(aPath); }
void CModSystem::SetEnabled(int Index, bool Enabled) { if(Index >= 0 && Index < static_cast<int>(m_vMods.size())) { auto &M = m_vMods[Index]; M.m_Enabled = Enabled; Changed(M); } }
void CModSystem::OnInit() { Storage()->CreateFolder("mods", IStorage::TYPE_SAVE); ReloadAll(false); }
void CModSystem::OnReset() { m_Executor.Reset(); m_LastTick = -1; m_LastDummy = -1; m_WasAlive = m_WasFrozen = m_WasGround = false; m_LastHookInput = m_LastJumpInput = false; m_LastHook = HOOK_IDLE; }
void CModSystem::OnShutdown() { OnReset(); }
void CModSystem::OnMapLoad() { OnReset(); }
void CModSystem::OnStateChange(int NewState, int OldState) { OnReset(); if(NewState == IClient::STATE_ONLINE && OldState != IClient::STATE_ONLINE) FireHook(HOOK_ON_CONNECT); }
void CModSystem::FireHook(EHook Hook, std::string_view Argument)
{
	if(!g_Config.m_XcModsEnabled || Client()->State() != IClient::STATE_ONLINE || m_Dispatching) return;
	m_Dispatching = true; m_Executor.Fire(m_vMods, Hook, Argument, Client()->GameTick(g_Config.m_ClDummy), *this); m_Dispatching = false;
}
void CModSystem::OnUpdate()
{
	if(g_Config.m_XcModsHotReload && time_get() - m_LastReload > time_freq()) { m_LastReload = time_get(); ReloadAll(); }
	if(!g_Config.m_XcModsEnabled) { if(m_WasEnabled) OnReset(); m_WasEnabled = false; return; }
	m_WasEnabled = true;
	if(Client()->State() != IClient::STATE_ONLINE) { m_Executor.Cancel(); return; }
	int Dummy = g_Config.m_ClDummy; if(m_LastDummy >= 0 && Dummy != m_LastDummy) OnReset(); m_LastDummy = Dummy;
	int Tick = Client()->GameTick(Dummy);
	if(m_LastTick != Tick) { m_LastTick = Tick; m_Executor.Advance(m_vMods, Tick, *this); FireHook(HOOK_ON_TICK); }
	const auto &Input = GameClient()->m_Controls.m_aInputData[Dummy];
	bool Hook = Input.m_Hook != 0, Jump = Input.m_Jump != 0;
	if(Hook && !m_LastHookInput) FireHook(HOOK_ON_HOOK_FIRE);
	if(Jump && !m_LastJumpInput) FireHook(HOOK_ON_JUMP);
	m_LastHookInput = Hook; m_LastJumpInput = Jump;
}
bool CModSystem::Grounded() const
{
	const auto *pChar = GameClient()->m_Snap.m_pLocalCharacter; if(!pChar) return false;
	return Collision()->CheckPoint(vec2(pChar->m_X - 14.0f, pChar->m_Y + 18.0f)) || Collision()->CheckPoint(vec2(pChar->m_X + 14.0f, pChar->m_Y + 18.0f));
}
void CModSystem::OnNewSnapshot()
{
	if(Client()->State() != IClient::STATE_ONLINE) return;
	const auto *pChar = GameClient()->m_Snap.m_pLocalCharacter; bool Alive = pChar != nullptr;
	if(Alive && !m_WasAlive) FireHook(HOOK_ON_SPAWN); if(!Alive && m_WasAlive) FireHook(HOOK_ON_DEATH);
	int Id = GameClient()->m_Snap.m_LocalClientId;
	bool Frozen = Alive && Id >= 0 && (GameClient()->m_aClients[Id].m_DeepFrozen || GameClient()->m_aClients[Id].m_FreezeEnd > Client()->GameTick(g_Config.m_ClDummy));
	bool Ground = Alive && Grounded();
	if(Ground && !m_WasGround) FireHook(HOOK_ON_GROUND);
	if(Frozen && !m_WasFrozen) FireHook(HOOK_ON_FREEZE); if(!Frozen && m_WasFrozen && Alive) FireHook(HOOK_ON_UNFREEZE);
	int Hook = pChar ? pChar->m_HookState : HOOK_IDLE; if(Hook == HOOK_GRABBED && m_LastHook != HOOK_GRABBED) FireHook(HOOK_ON_HOOK_ATTACH);
	m_WasAlive = Alive; m_WasFrozen = Frozen; m_WasGround = Ground; m_LastHook = Hook;
}
void CModSystem::OnMessage(int Msg, void *pRawMsg) { if(Msg == NETMSGTYPE_SV_RACEFINISH && static_cast<CNetMsg_Sv_RaceFinish *>(pRawMsg)->m_ClientId == GameClient()->m_Snap.m_LocalClientId) FireHook(HOOK_ON_FINISH); }
bool CModSystem::OnInput(const IInput::CEvent &Event)
{
	if((Event.m_Flags & IInput::FLAG_PRESS) && !GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive() && !GameClient()->m_GameConsole.IsActive()) FireHook(HOOK_ON_KEY_PRESS, Input()->KeyName(Event.m_Key)); return false;
}
double CModSystem::AimAngle() const { vec2 Aim = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy]; if(length(Aim) < 0.001f) Aim = vec2(0, -1); return std::atan2(Aim.y, Aim.x) * 180.0 / PI; }
void CModSystem::SetAngle(double Degrees)
{
	if(!std::isfinite(Degrees) || !GameClient()->m_Snap.m_pLocalCharacter) return;
	int Dummy = g_Config.m_ClDummy; auto &Controls = GameClient()->m_Controls;
	float Radius = std::max(1.0f, length(Controls.m_aMousePos[Dummy])); double Rad = std::remainder(Degrees, 360.0) * PI / 180.0;
	Controls.m_aMousePos[Dummy] = vec2(static_cast<float>(std::cos(Rad) * Radius), static_cast<float>(std::sin(Rad) * Radius));
	Controls.m_aInputData[Dummy].m_TargetX = static_cast<int>(Controls.m_aMousePos[Dummy].x); Controls.m_aInputData[Dummy].m_TargetY = static_cast<int>(Controls.m_aMousePos[Dummy].y);
	if(!Controls.m_aInputData[Dummy].m_TargetX && !Controls.m_aInputData[Dummy].m_TargetY) Controls.m_aInputData[Dummy].m_TargetY = -1;
}
SNearest CModSystem::Trace(ENearestTarget Target, double Angle) const
{
	SNearest Out; const auto *pChar = GameClient()->m_Snap.m_pLocalCharacter; if(!pChar) return Out;
	vec2 Origin(pChar->m_X, pChar->m_Y), Dir(static_cast<float>(std::cos(Angle * PI / 180)), static_cast<float>(std::sin(Angle * PI / 180)));
	float Range = std::max(0.0f, static_cast<float>(GameClient()->m_aTuning[g_Config.m_ClDummy].m_HookLength));
	vec2 Wall, Before; int Hit = Collision()->IntersectLineTeleHook(Origin + Dir * 42.0f, Origin + Dir * Range, &Wall, &Before);
	float WallDistance = Hit ? distance(Origin, Wall) : Range;
	bool WantPlayer = Target == TARGET_PLAYER || Target == TARGET_HOOKABLE || Target == TARGET_ANY;
	if(WantPlayer)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			const auto &C = GameClient()->m_Snap.m_aCharacters[i]; if(i == GameClient()->m_Snap.m_LocalClientId || !C.m_Active || GameClient()->IsOtherTeam(i)) continue;
			vec2 Pos(C.m_Cur.m_X, C.m_Cur.m_Y), Delta = Pos - Origin; float Along = dot(Delta, Dir), Perp = length(Delta - Dir * Along);
			if(Along <= 0 || Along > Range || Perp > 30.0f) continue;
			float Entry = std::max(0.0f, Along - std::sqrt(900.0f - Perp * Perp)); if(Entry > WallDistance || (Out.m_Valid && Entry >= Out.m_Distance)) continue;
			Out.m_Valid = true; Out.m_Player = i; Out.m_X = Pos.x; Out.m_Y = Pos.y; Out.m_Distance = Entry; Out.m_Angle = std::atan2(Delta.y, Delta.x) * 180 / PI;
		}
	}
	bool WantWall = Target == TARGET_ANY || (Hit == TILE_SOLID && (Target == TARGET_SOLID || Target == TARGET_HOOKABLE)) || (Hit == TILE_NOHOOK && Target == TARGET_IRON);
	if(Hit && WantWall && (!Out.m_Valid || WallDistance < Out.m_Distance)) { Out.m_Valid = true; Out.m_Player = -1; Out.m_X = Wall.x; Out.m_Y = Wall.y; Out.m_Distance = WallDistance; Out.m_Angle = Angle; }
	return Out;
}
bool CModSystem::Condition(const CCondition &Cond, const modsystem::CMod &Mod, SRuntimeState &State)
{
	const auto *pChar = GameClient()->m_Snap.m_pLocalCharacter;
	if(!pChar) return false;
	vec2 Aim = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
	switch(Cond.m_Type) {
	case COND_HOOK_WILL_HIT_PLAYER: return Trace(TARGET_ANY, AimAngle()).m_Player >= 0;
	case COND_HOOK_WILL_HIT_SOLID: return Trace(TARGET_SOLID, AimAngle()).m_Valid;
	case COND_HOOK_WILL_HIT_IRON: return Trace(TARGET_IRON, AimAngle()).m_Valid;
	case COND_HOOK_IS_READY: return pChar->m_HookState == HOOK_IDLE || pChar->m_HookState == HOOK_RETRACTED;
	case COND_HOOK_AIM_LEFT: return Aim.x < 0; case COND_HOOK_AIM_RIGHT: return Aim.x > 0; case COND_HOOK_AIM_UP: return Aim.y < 0; case COND_HOOK_AIM_DOWN: return Aim.y > 0;
	case COND_IS_ON_GROUND: return Grounded(); case COND_IS_AIRBORNE: return !Grounded(); default: return false;
	}
}
void CModSystem::Action(const CBlock &Block, const modsystem::CMod &Mod, SRuntimeState &State)
{
	double Number = 0; auto Kind = ActionParamKind(Block.m_Action);
	if(Kind == PARAM_INT && !ResolveNumber(Mod, State, Block.m_Argument.empty() ? "0" : Block.m_Argument, Number)) return;
	std::string Text;
	if(Kind == PARAM_STR) Text = ResolveText(Mod, State, Block.m_Argument);
	switch(Block.m_Action)
	{
	case ACT_SET_HOOK_ANGLE: SetAngle(Number); break;
	case ACT_SEND_CHAT: GameClient()->m_Chat.SendChat(0, Text.c_str()); break;
	case ACT_RUN_COMMAND: Console()->ExecuteLine(Text.c_str(), IConsole::CLIENT_ID_UNSPECIFIED); break;
	case ACT_ECHO: GameClient()->Echo(Text.c_str()); break;
	case ACT_PLAY_SOUND:
		if(Number >= 0 && Number < g_pData->m_NumSounds)
			GameClient()->m_Sounds.Play(CSounds::CHN_GUI, static_cast<int>(Number), 1.0f);
		else
			State.m_Error = "声音编号超出范围";
		break;
	case ACT_SET_COLOR:
		if(Number < 0 || Number > 0xffffff) { State.m_Error = "颜色范围为 0 到 16777215"; break; }
		if(g_Config.m_ClDummy) { g_Config.m_ClDummyUseCustomColor = 1; g_Config.m_ClDummyColorBody = static_cast<unsigned>(Number); GameClient()->SendDummyInfo(false); } else { g_Config.m_ClPlayerUseCustomColor = 1; g_Config.m_ClPlayerColorBody = static_cast<unsigned>(Number); GameClient()->SendInfo(false); } break;
	case ACT_GET_VALUE:
	{
		const auto *pChar = GameClient()->m_Snap.m_pLocalCharacter;
		// Target values describe this ray only; never scan or select a nearby aim.
		if(Block.m_Value >= GETVAL_NEAREST_X) State.m_Nearest = Trace(Block.m_Target, AimAngle());
		const auto &N = State.m_Nearest; double X = pChar ? pChar->m_X : 0, Y = pChar ? pChar->m_Y : 0;
		switch(Block.m_Value) { case GETVAL_POS_X: Number = X; break; case GETVAL_POS_Y: Number = Y; break; case GETVAL_VEL_X: Number = pChar ? pChar->m_VelX / 256.0 : 0; break; case GETVAL_VEL_Y: Number = pChar ? pChar->m_VelY / 256.0 : 0; break; case GETVAL_HOOK_ANGLE: Number = AimAngle(); break; case GETVAL_NEAREST_X: Number = N.m_Valid ? N.m_X : 0; break; case GETVAL_NEAREST_Y: Number = N.m_Valid ? N.m_Y : 0; break; case GETVAL_NEAREST_DISTANCE: Number = N.m_Valid ? std::hypot(N.m_X - X, N.m_Y - Y) : 0; break; case GETVAL_NEAREST_ANGLE: Number = N.m_Valid ? std::atan2(N.m_Y - Y, N.m_X - X) * 180 / PI : AimAngle(); break; case GETVAL_NEAREST_DX: Number = N.m_Valid ? N.m_X - X : 0; break; case GETVAL_NEAREST_DY: Number = N.m_Valid ? N.m_Y - Y : 0; break; case GETVAL_NEAREST_VALID: Number = N.m_Valid ? 1 : 0; break; default: break; }
		State.m_Register = Number; break;
	}
	default: break;
	}
}
