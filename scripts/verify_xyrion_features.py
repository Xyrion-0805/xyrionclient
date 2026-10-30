"""Capture feature smoke checks in isolated, non-saving local profiles."""
import argparse
import json
from pathlib import Path
import shutil
import socket
import subprocess
import time
import ctypes
from ctypes import wintypes

from verify_video_background import quote, send_command


def focus(process):
    user = ctypes.windll.user32
    user.GetForegroundWindow.restype = wintypes.HWND
    user.SetForegroundWindow.argtypes = [wintypes.HWND]
    user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    windows = []
    @callback_type
    def callback(hwnd, _):
        pid = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == process.pid and user.IsWindowVisible(hwnd):
            windows.append(hwnd)
        return True
    user.EnumWindows(callback, 0)
    if windows:
        # Screenshots intentionally require a focused game window.
        user.ShowWindow(windows[0], 9)
        user.keybd_event(0x12, 0, 0, 0)
        user.SetForegroundWindow(windows[0])
        user.keybd_event(0x12, 0, 2, 0)
        time.sleep(0.5)
    return windows[0] if windows else None


def press_key(key):
    user = ctypes.windll.user32
    user.keybd_event(key, 0, 0, 0)
    time.sleep(0.1)
    user.keybd_event(key, 0, 2, 0)


def capture(process, pipe, root, name):
    focus(process)
    directory = root / "screenshots"
    before = set(directory.glob("*.png"))
    send_command(pipe, "screenshot", process)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        created = set(directory.glob("*.png")) - before
        if created:
            screenshot = max(created, key=lambda p: p.stat().st_mtime)
            time.sleep(0.3)
            destination = root / (name + ".png")
            shutil.copyfile(screenshot, destination)
            return str(destination)
        if process.poll() is not None:
            raise RuntimeError("Client exited before screenshot")
        time.sleep(0.2)
    raise RuntimeError("Screenshot did not appear")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--video", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    (root / "maps").mkdir(exist_ok=True)
    shutil.copyfile(args.map, root / "maps" / args.map.name)
    (root / "storage.cfg").write_text("add_path $CURRENTDIR\nadd_path $DATADIR\n", encoding="utf-8")
    (root / "autoexec_server.cfg").write_text("", encoding="utf-8")
    port = 18307
    pipe = "xyrion_feature_smoke"
    server_cfg = root / "server.cfg"
    server_cfg.write_text(f"bindaddr 127.0.0.1\nsv_port {port}\nsv_register 0\nsv_sixup 0\nsv_map {args.map.stem}\nsv_test_cmds 1\nsv_rcon_password xyrion-local-smoke\n", encoding="utf-8")
    client_cfg = root / "client.cfg"
    client_cfg.write_text(
        "cl_save_settings 0\ncl_show_welcome 0\ncl_skip_start_menu 0\n"
        "cl_auto_demo_record 0\ncl_auto_race_record 0\ncl_menu_map \"\"\n"
        "snd_enable 0\ngfx_fullscreen 0\ngfx_screen_width 1280\ngfx_screen_height 800\n"
        "gfx_vsync 1\ncl_refresh_rate 120\ncl_refresh_rate_inactive 120\n"
        "gfx_gl_major 3\ngfx_gl_minor 0\nqm_hud_island_enable 1\n"
        "bc_finish_prediction 1\nbc_chat_bubbles 1\nbc_animations 1\n"
        "player_name XyrionSmoke\nxc_video_enabled 1\nxc_video_dim 30\n"
        f"xc_video_path {quote(args.video.resolve())}\ncl_input_fifo {pipe}\n", encoding="utf-8")
    result = {"screenshots": {}, "profile": str(root), "server_available": False}
    with (root / "server.log").open("wb") as server_log, (root / "client.log").open("wb") as client_log:
        server = subprocess.Popen([str(args.server.resolve()), "-f", str(server_cfg)], cwd=root, stdout=server_log, stderr=subprocess.STDOUT)
        client = None
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(1)
                for _ in range(12):
                    if server.poll() is not None:
                        raise RuntimeError("Local test server exited")
                    sock.sendto(b"\xff" * 10 + b"gie3\x01", ("127.0.0.1", port))
                    try:
                        reply, _ = sock.recvfrom(65535)
                        if b"inf3" in reply and args.map.stem.encode() in reply:
                            result["server_available"] = True
                            break
                    except (socket.timeout, ConnectionResetError):
                        time.sleep(0.5)
            if not result["server_available"]:
                raise RuntimeError("Local test server did not answer")
            client = subprocess.Popen([str(args.client.resolve()), "-f", str(client_cfg)], cwd=root, stdout=client_log, stderr=subprocess.STDOUT)
            send_command(pipe, "echo feature_smoke_ready", client)
            time.sleep(8)
            result["screenshots"]["menu_no_island"] = capture(client, pipe, root, "menu_no_island")
            time.sleep(1)
            result["screenshots"]["video_next_frame"] = capture(client, pipe, root, "video_next_frame")
            send_command(pipe, "ui_settings_page 11", client)
            focus(client)
            press_key(0x53)  # Start menu's Settings shortcut.
            time.sleep(2)
            result["screenshots"]["xyrion_settings"] = capture(client, pipe, root, "xyrion_settings")
            send_command(pipe, "xc_global_glass 1", client)
            time.sleep(1)
            result["screenshots"]["glass_settings"] = capture(client, pipe, root, "glass_settings")
            send_command(pipe, "cl_editor 1", client)
            time.sleep(3)
            result["screenshots"]["chinese_editor"] = capture(client, pipe, root, "chinese_editor")
            send_command(pipe, "cl_editor 0", client)
            send_command(pipe, f"connect 127.0.0.1:{port}", client)
            time.sleep(8)
            result["screenshots"]["bestclient_hud"] = capture(client, pipe, root, "bestclient_hud")
            send_command(pipe, "say BestClient chat bubble", client)
            time.sleep(0.5)
            result["screenshots"]["chat_bubble"] = capture(client, pipe, root, "chat_bubble")
            send_command(pipe, 'chat all BestClient typing', client)
            time.sleep(0.5)
            result["screenshots"]["chat_input"] = capture(client, pipe, root, "chat_input")
            press_key(0x1B)
            send_command(pipe, 'chat all /', client)
            time.sleep(1)
            result["screenshots"]["command_hints"] = capture(client, pipe, root, "command_hints")
            press_key(0x1B)
            send_command(pipe, "rcon_auth xyrion-local-smoke", client)
            time.sleep(1)
            send_command(pipe, "rcon move 83 -2", client)
            time.sleep(2)
            result["screenshots"]["gores_freeze"] = capture(client, pipe, root, "gores_freeze")
            send_command(pipe, "xc_auto_translate_chinese 1", client)
            send_command(pipe, "rcon say Hello world", client)
            time.sleep(8)
            send_command(pipe, 'rcon say 你好，世界', client)
            time.sleep(1)
            result["screenshots"]["chinese_translation"] = capture(client, pipe, root, "chinese_translation")
            # Reconnect exercises the component reset and map-load lifecycle.
            send_command(pipe, "disconnect", client)
            time.sleep(1)
            focus(client)
            press_key(0x1B)
            send_command(pipe, "ui_page 5", client)
            press_key(0x50)
            time.sleep(5)
            result["screenshots"]["server_map_preview"] = capture(client, pipe, root, "server_map_preview")
            send_command(pipe, f"connect 127.0.0.1:{port}", client)
            time.sleep(6)
            result["screenshots"]["gores_reconnect"] = capture(client, pipe, root, "gores_reconnect")
            send_command(pipe, "quit", client)
            client.wait(timeout=15)
            result["client_exit_code"] = client.returncode
            if client.returncode:
                raise RuntimeError("Client did not exit cleanly")
        finally:
            for process in [client, server]:
                if process and process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
    text = (root / "client.log").read_text(encoding="utf-8", errors="replace")
    result["localization_errors"] = [line for line in text.splitlines() if "localization" in line and any(word in line.lower() for word in ["malformed", "unexpected", "error"])]
    result["video_logs"] = [line for line in text.splitlines() if "video_background" in line]
    result["console_errors"] = [line for line in text.splitlines() if "No such command" in line]
    result["glass_logs"] = [line for line in text.splitlines() if "ui_glass" in line]
    result["limitations"] = "Local startup/render/freeze/reconnect smoke; lyric availability depends on LRCLIB and media metadata."
    (root / "result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
