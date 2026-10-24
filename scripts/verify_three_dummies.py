"""Verify four real local players, dummy selection, movement and reconnects."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time

from verify_xyrion_features import focus


def capture_window(process, root):
    # Capture this test window only. PrintWindow also works for OpenGL windows
    # when engine screenshots are skipped because of desktop focus changes.
    command = r'''
Add-Type -AssemblyName System.Drawing
Add-Type 'using System; using System.Runtime.InteropServices; public class DummyCapture { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags); [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect); public struct RECT {public int Left, Top, Right, Bottom;} }'
[DummyCapture]::SetProcessDPIAware() | Out-Null
$testProcess = Get-Process -Id ([int]$env:XYRION_CAPTURE_PID)
$testRect = New-Object DummyCapture+RECT
[DummyCapture]::GetClientRect($testProcess.MainWindowHandle, [ref]$testRect) | Out-Null
$testBitmap = New-Object System.Drawing.Bitmap($testRect.Right,$testRect.Bottom)
$testGraphics = [System.Drawing.Graphics]::FromImage($testBitmap)
$testDc = $testGraphics.GetHdc()
[DummyCapture]::PrintWindow($testProcess.MainWindowHandle,$testDc,3) | Out-Null
$testGraphics.ReleaseHdc($testDc)
$testBitmap.Save($env:XYRION_CAPTURE_PATH)
$testGraphics.Dispose()
$testBitmap.Dispose()
'''
    env = os.environ.copy()
    destination = root / "dummy-selector.png"
    env.update(XYRION_CAPTURE_PID=str(process.pid), XYRION_CAPTURE_PATH=str(destination))
    subprocess.run(["powershell.exe", "-NoProfile", "-Command", command], env=env, check=True, timeout=20, capture_output=True)
    return str(destination)


class Pipe:
    def __init__(self, name, process):
        self.process = process
        deadline = time.monotonic() + 25
        while True:
            try:
                self.file = open("\\\\.\\pipe\\" + name, "wb", buffering=0)
                return
            except OSError:
                if process.poll() is not None or time.monotonic() > deadline:
                    raise
                time.sleep(.1)

    def send(self, command):
        if self.process.poll() is not None:
            raise RuntimeError(f"Process exited: {self.process.returncode}")
        self.file.write((command + "\n").encode())

    def close(self):
        self.file.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sixup", action="store_true")
    parser.add_argument("--skip-movement", action="store_true")
    parser.add_argument("--capture-ui", action="store_true")
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    (root / "storage.cfg").write_text("add_path $CURRENTDIR\nadd_path $DATADIR\n", encoding="utf-8")
    (root / "autoexec_server.cfg").write_text("", encoding="utf-8")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    client_pipe = f"xyrion_dummy_client_{os.getpid()}"
    server_pipe = f"xyrion_dummy_server_{os.getpid()}"
    server_cfg = root / "server.cfg"
    server_cfg.write_text(
        f"bindaddr 127.0.0.1\nsv_port {port}\nsv_register 0\nsv_sixup {int(args.sixup)}\n"
        'sv_map "Sunny Side Up"\nsv_max_clients_per_ip 4\nsv_connlimit 100\nsv_test_cmds 1\n'
        f"sv_input_fifo {server_pipe}\n", encoding="utf-8")
    client_cfg = root / "client.cfg"
    address = f"{'tw-0.7+udp://' if args.sixup else ''}127.0.0.1:{port}"
    client_cfg.write_text(
        'cl_save_settings 0\ncl_show_welcome 0\ncl_skip_start_menu 1\ncl_menu_map ""\n'
        'snd_enable 0\ngfx_fullscreen 0\ngfx_vsync 1\ngfx_screen_width 960\ngfx_screen_height 640\n'
        'cl_refresh_rate 60\ncl_refresh_rate_inactive 60\ncl_auto_demo_record 0\ncl_auto_race_record 0\n'
        'gfx_gl_major 3\ngfx_gl_minor 3\nxc_video_enabled 0\nqm_steam_auto_launch 0\n'
        'player_name XyrionDummyTest\ndummy_name TestDummy\ncl_dummy_resetonswitch 1\n'
        f'cl_input_fifo {client_pipe}\nconnect {address}\n', encoding="utf-8")
    result = {"sixup": args.sixup, "checkpoints": []}
    client_log = root / "client.log"

    with (root / "server.log").open("wb") as slog, client_log.open("wb") as clog:
        server = subprocess.Popen([str(args.server.resolve()), "-f", str(server_cfg)], cwd=root, stdout=slog, stderr=subprocess.STDOUT)
        client = None
        cp = sp = None
        try:
            sp = Pipe(server_pipe, server)
            client = subprocess.Popen([str(args.client.resolve()), "-f", str(client_cfg)], cwd=root, stdout=clog, stderr=subprocess.STDOUT)
            cp = Pipe(client_pipe, client)

            def status():
                offset = client_log.stat().st_size
                cp.send("dummy_status")
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    data = client_log.read_bytes()[offset:].decode("utf-8", errors="replace")
                    header = re.search(r"connected=(\d)/3 selected=(\d)", data)
                    rows = re.findall(r"dummy=\d connected=\d ack=-?\d+ tick=-?\d+ local=-?\d+ x=-?\d+ y=-?\d+ direction=-?\d+ hook=\d", data)
                    if header and len(rows) == 3:
                        return {"count": int(header[1]), "selected": int(header[2]), "dummies": {
                            int(values["dummy"]): values for values in
                            ({k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", row)} for row in rows)}}
                    if client.poll() is not None:
                        raise RuntimeError(f"Client exited: {client.returncode}")
                    time.sleep(.1)
                raise TimeoutError("Console did not respond")

            def wait_for(predicate, timeout=20):
                deadline = time.monotonic() + timeout
                latest = None
                while time.monotonic() < deadline:
                    latest = status()
                    if predicate(latest):
                        return latest
                    time.sleep(.3)
                raise TimeoutError(f"Dummy state did not converge: {latest}")

            def checkpoint(name, state):
                result["checkpoints"].append({"name": name, "state": state})
                (root / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
                print(name, json.dumps(state), flush=True)

            # Main must have reached online before the first dummy command.
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                if "entered and joined the game" in client_log.read_text(encoding="utf-8", errors="replace"):
                    break
                time.sleep(.2)
            time.sleep(2)
            for number in range(1, 4):
                cp.send("dummy_connect")
                state = wait_for(lambda s: s["count"] == number and all(d["ack"] > 0 and d["local"] >= 0 for d in s["dummies"].values() if d["connected"]))
                checkpoint(f"connect_{number}", state)
            ids = {d["local"] for d in state["dummies"].values()}
            if len(ids) != 3 or 0 in ids:
                raise AssertionError(f"Dummies do not have distinct server player IDs: {ids}")
            if args.capture_ui:
                focus(client)
                ctypes.windll.user32.keybd_event(0, 1, 8, 0)
                time.sleep(.1)
                ctypes.windll.user32.keybd_event(0, 1, 10, 0)
                time.sleep(.7)
                result["selector_screenshot"] = capture_window(client, root)
                ctypes.windll.user32.keybd_event(0, 1, 8, 0)
                time.sleep(.1)
                ctypes.windll.user32.keybd_event(0, 1, 10, 0)
                time.sleep(.3)
            for number in [2, 3, 1, 3, 2, 0]:
                cp.send(f"dummy_select {number}")
                state = wait_for(lambda s: s["selected"] == number and s["count"] == 3)
                if number and not args.skip_movement:
                    before = state["dummies"][number]["x"]
                    hwnd = focus(client)
                    foreground = ctypes.windll.user32.GetForegroundWindow()
                    print("movement_window", hwnd, "foreground", foreground, flush=True)
                    ctypes.windll.user32.keybd_event(0, 0x20, 8, 0)
                    try:
                        moved = wait_for(lambda s: s["dummies"][number]["direction"] == 1 and abs(s["dummies"][number]["x"] - before) > 8, timeout=60)
                    finally:
                        ctypes.windll.user32.keybd_event(0, 0x20, 10, 0)
                    state = wait_for(lambda s: s["dummies"][number]["direction"] == 0)
                    checkpoint(f"control_{number}", moved)
                else:
                    checkpoint("return_to_main", state)
            if not args.skip_movement:
                cp.send("cl_dummy_copy_moves 1")
                focus(client)
                ctypes.windll.user32.keybd_event(0, 0x20, 8, 0)
                try:
                    checkpoint("copy_moves_all", wait_for(lambda s: all(d["direction"] == 1 for d in s["dummies"].values())))
                finally:
                    ctypes.windll.user32.keybd_event(0, 0x20, 10, 0)
                wait_for(lambda s: all(d["direction"] == 0 for d in s["dummies"].values()))
                cp.send("cl_dummy_copy_moves 0")
                cp.send("dummy_select 2")
                wait_for(lambda s: s["selected"] == 2)
                cp.send("bind h +hook")
                focus(client)
                ctypes.windll.user32.keybd_event(0, 0x23, 8, 0)
                try:
                    wait_for(lambda s: s["dummies"][2]["hook"] == 1)
                    cp.send("dummy_select 3")
                    checkpoint("switch_releases_hook", wait_for(lambda s: s["selected"] == 3 and s["dummies"][2]["hook"] == 0))
                finally:
                    ctypes.windll.user32.keybd_event(0, 0x23, 10, 0)
            cp.send("dummy_disconnect 2")
            checkpoint("disconnect_only_2", wait_for(lambda s: s["count"] == 2 and not s["dummies"][2]["connected"]))
            cp.send("dummy_connect")
            checkpoint("reconnect_2", wait_for(lambda s: s["count"] == 3 and s["dummies"][2]["ack"] > 0))
            cp.send("dummy_select 2")
            wait_for(lambda s: s["selected"] == 2)
            cp.send("dummy_disconnect 2")
            checkpoint("disconnect_active_2", wait_for(lambda s: s["count"] == 2 and s["selected"] not in (0, 2) and not s["dummies"][2]["connected"]))
            cp.send("dummy_connect")
            wait_for(lambda s: s["count"] == 3 and s["dummies"][2]["ack"] > 0)
            cp.send("dummy_select 3")
            state = wait_for(lambda s: s["selected"] == 3)
            sp.send(f'kick {state["dummies"][3]["local"]} local_dummy_test')
            checkpoint("server_kick_active_3", wait_for(lambda s: s["count"] == 2 and s["selected"] not in (0, 3)))
            cp.send("dummy_connect")
            wait_for(lambda s: s["count"] == 3 and s["dummies"][3]["ack"] > 0)
            cp.send("dummy_select 3")
            wait_for(lambda s: s["selected"] == 3)
            sp.send("reload")
            time.sleep(2)
            checkpoint("map_reload", wait_for(lambda s: s["count"] == 3 and s["selected"] == 3 and all(d["ack"] > 0 for d in s["dummies"].values()), timeout=30))
            cp.send("dummy_disconnect")
            checkpoint("disconnect_all", wait_for(lambda s: s["count"] == 0 and s["selected"] == 0))
            cp.send("quit")
            client.wait(timeout=15)
            if client.returncode != 0:
                raise RuntimeError(f"Client exit: {client.returncode}")
            result["passed"] = True
        finally:
            if cp:
                cp.close()
            if sp:
                sp.close()
            for process in [client, server]:
                if process and process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
            (root / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
