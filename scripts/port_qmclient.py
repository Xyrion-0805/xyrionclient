"""Copy the selected QmClient implementations; keep a pre-port source snapshot.

Run from XyrionClient. Upstream: wxj881027/QmClient,
2b626303c0985baf70c9fb599b26a4f35b8ac8c5.
"""
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT.parent / "QmClient"
TMP = ROOT / "tmp" / "qm-port"
TMP.mkdir(parents=True, exist_ok=True)


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path, content):
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8", newline="\n")


def patch(path, before, after):
    content = read(path)
    assert content.count(before) == 1, (path, before[:100], content.count(before))
    write(path, content.replace(before, after))


def copy(path):
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(UPSTREAM / path, target)


def upstream(path):
    return (UPSTREAM / path).read_text(encoding="utf-8-sig")


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    backup = TMP / "pre-port-source.zip"
    if not backup.exists():
        with zipfile.ZipFile(backup, "w", zipfile.ZIP_DEFLATED) as archive:
            for directory in ("src", "scripts", "docs/xyrion"):
                for path in (ROOT / directory).rglob("*"):
                    if path.is_file():
                        archive.write(path, path.relative_to(ROOT))
            archive.write(ROOT / "CMakeLists.txt", "CMakeLists.txt")

    for path in (
        "src/game/client/components/qmclient/weapon_trajectory.cpp",
        "src/game/client/components/qmclient/weapon_trajectory.h",
        "src/game/client/components/qmclient/gores_drown_tracker.h",
        "src/engine/client/steam.cpp", "src/engine/client/steam_probe.cpp",
        "src/engine/client/steam_probe.h", "src/engine/client/steam_client_path.h",
        "src/engine/steam.h",
    ):
        copy(path)

    # Qm uses the older combined base header and shell_execute spelling.
    for path in ("src/engine/client/steam.cpp", "src/engine/client/steam_probe.cpp"):
        content = read(path).replace("#include <base/system.h>",
            "#include <base/dbg.h>\n#include <base/str.h>\n#include <base/net.h>\n#include <base/process.h>")
        content = content.replace("shell_execute(", "process_execute(")
        write(path, content)

    path = "src/game/client/components/qmclient/weapon_trajectory.cpp"
    content = read(path).replace("#include <game/client/components/qmclient/modes.h>\n", "")
    # These gates belong to Qm's separate Zen/Gores modes, absent in Xyrion.
    content = content.replace("\tif(GetQmFocusModeDecisions().m_HideGuideLines)\n\t\treturn false;\n\n", "")
    content = content.replace("\tif(ShouldHideFocusGuideLines(g_Config.m_QmFocusMode != 0, g_Config.m_QmFocusModeHideGuideLines != 0))\n\t\treturn false;\n", "")
    content = content.replace("return TrajectoryVisible && !GameClient()->m_TClient.ShouldHideGoresGuides(TrajectoryVisible);", "return TrajectoryVisible;")
    write(path, content)

    config = upstream("src/engine/shared/config_variables_qmclient.h")
    prefixes = ("QmSteam", "QmWeaponTrajectory", "QmCursorScale", "QmGoresDrownBoard")
    selected = [line for line in config.splitlines() if line.startswith("MACRO_CONFIG_") and
                re.search(r"\((?:" + "|".join(prefixes) + r")", line)]
    write("src/engine/shared/config_variables_qmclient.h", "// QmClient imported configuration; included multiple times.\n" + "\n".join(selected) + "\n")
    patch("src/engine/shared/config_includes.h", '#include "config_variables_bestclient.h"',
          '#include "config_variables_bestclient.h"\n#include "config_variables_qmclient.h"')

    patch("src/game/client/gameclient.h", '#include "components/bestclient/inputs.h"',
        '#include "components/bestclient/inputs.h"\n#include "components/qmclient/weapon_trajectory.h"')
    patch("src/game/client/gameclient.h", "\tCPlayers m_Players;", "\tCPlayers m_Players;\n\tCQmWeaponTrajectory m_QmWeaponTrajectory;")
    patch("src/game/client/gameclient.cpp", "\t\t\t\t\t      &m_Players,", "\t\t\t\t\t      &m_QmWeaponTrajectory,\n\t\t\t\t\t      &m_Players,")
    patch("src/game/client/components/controls.h", "\tint m_aShowHookColl[NUM_DUMMIES];", "\tint m_aShowHookColl[NUM_DUMMIES];\n\tint m_aShowWeaponTrajectory[NUM_DUMMIES] = {};")
    path = "src/game/client/components/controls.cpp"
    source = upstream(path)
    match = re.search(r"\t\{\n\t\tstatic CInputState s_State = .*?\+showweapontrajectory.*?\n\t\}", source, re.S)
    # Copy the exact key registration block, without unrelated preceding blocks.
    marker = source.index('Console()->Register("+showweapontrajectory"')
    begin = source.rfind("\t{", 0, marker)
    end = source.index("\n\t}", marker) + 3
    keyblock = source[begin:end]
    patch(path, "void CControls::OnConsoleInit()\n{", "void CControls::OnConsoleInit()\n{\n" + keyblock)
    patch("src/game/client/components/players.cpp",
        "\t\tRenderHookCollLine(ScreenRect, &pClientData->m_RenderPrev, &pClientData->m_RenderCur, RenderLastId);",
        "\t\tRenderHookCollLine(ScreenRect, &pClientData->m_RenderPrev, &pClientData->m_RenderCur, RenderLastId);\n\t\tGameClient()->m_QmWeaponTrajectory.Render(&pClientData->m_RenderPrev, &pClientData->m_RenderCur, RenderLastId);")

    path = "src/game/client/components/tclient/tclient.h"
    patch(path, "#include <deque>", "#include <deque>\n#include <game/client/components/qmclient/gores_drown_tracker.h>")
    patch(path, "\tCTClient();", "\tCTClient();\n\tCQmGoresDrownTracker m_GoresDrownTracker;\n\tchar m_aGoresDrownMap[128] = {};\n\tbool m_GoresDrownModeActive = false;\n\tvoid ResetGoresDrownCounts();\n\tvoid UpdateGoresDrownCounts();\n\tbool IsGoresDrownBoardMode() const;\n\tbool IsGoresGameMode() const;\n\tint GetGoresDrownCount(int ClientId) const { return m_GoresDrownTracker.Count(ClientId); }")
    path = "src/game/client/components/tclient/tclient.cpp"
    source = upstream(path)
    additions = [function(source, signature) for signature in (
        "void CTClient::ResetGoresDrownCounts()", "void CTClient::UpdateGoresDrownCounts()",
        "bool CTClient::IsGoresDrownBoardMode() const", "bool CTClient::IsGoresGameMode() const")]
    additions = [s.replace("Client()->GetCurrentMap()", "GameClient()->Map()->BaseName()") for s in additions]
    write(path, read(path) + "\n\n" + "\n\n".join(additions) + "\n")
    patch(path, "void CTClient::OnStateChange(int OldState, int NewState)\n{", "void CTClient::OnStateChange(int OldState, int NewState)\n{\n\tResetGoresDrownCounts();")
    patch(path, "void CTClient::OnNewSnapshot()\n{", "void CTClient::OnNewSnapshot()\n{\n\tUpdateGoresDrownCounts();")

    patch("CMakeLists.txt", "    steam.cpp", "    steam.cpp\n    steam_probe.cpp\n    steam_probe.h\n    steam_client_path.h")
    patch("CMakeLists.txt", "  config_variables_bestclient.h", "  config_variables_bestclient.h\n  config_variables_qmclient.h")
    patch("CMakeLists.txt", "    components/bestclient/inputs.cpp", "    components/qmclient/weapon_trajectory.cpp\n    components/qmclient/weapon_trajectory.h\n    components/qmclient/gores_drown_tracker.h\n    components/bestclient/inputs.cpp")
    print("Imported weapon trajectory, Steam and Gores tracking; backup:", backup)


if __name__ == "__main__":
    main()
