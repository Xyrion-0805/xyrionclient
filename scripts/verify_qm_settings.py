"""Exercise settings and tooltip text creation in an isolated Windows profile."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import shutil
import time

from verify_video_background import send_command as startup_send_command
from verify_xyrion_features import focus, press_key


def point(hwnd, x, y):
    user = ctypes.windll.user32
    user.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    pos = wintypes.POINT(x, y)
    user.ClientToScreen(hwnd, ctypes.byref(pos))
    user.SetCursorPos(pos.x, pos.y)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    (root / "storage.cfg").write_text("add_path $CURRENTDIR\nadd_path $DATADIR\n", encoding="utf-8")
    pipe = f"xyrion_qm_settings_{os.getpid()}"
    cfg = root / "client.cfg"
    cfg.write_text(
        'cl_save_settings 0\ncl_show_welcome 0\ncl_skip_start_menu 0\ncl_menu_map ""\n'
        'snd_enable 0\ngfx_fullscreen 0\ngfx_vsync 1\ngfx_screen_width 1280\ngfx_screen_height 800\n'
        'gfx_gl_major 3\ngfx_gl_minor 3\nxc_video_enabled 0\nui_settings_page 1\n'
        'cl_languagefile "languages/simplified_chinese.txt"\n'
        f'cl_input_fifo {pipe}\n', encoding="utf-8")
    result = {"client": str(args.client.resolve()), "pages": [], "screenshots": {}}
    def capture(process, pipe, root, name):
        focus(process)
        directory = root / "screenshots"
        before = set(directory.glob("*.png"))
        send_command(pipe, "screenshot", process)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            created = set(directory.glob("*.png")) - before
            if created:
                screenshot = max(created, key=lambda p: p.stat().st_mtime)
                time.sleep(.3)
                destination = root / (name + ".png")
                shutil.copyfile(screenshot, destination)
                return str(destination)
            time.sleep(.2)
        raise RuntimeError("Screenshot did not appear")

    with (root / "client.log").open("wb") as log:
        process = subprocess.Popen([str(args.client.resolve()), "-f", str(cfg)], cwd=root, stdout=log, stderr=subprocess.STDOUT)
        try:
            # Retain this named-pipe connection until shutdown, so commands
            # cannot be discarded when a slow external Steam callback delays
            # the next engine update beyond the helper's short write window.
            deadline = time.monotonic() + 15
            while True:
                try:
                    connection = open("\\\\.\\pipe\\" + pipe, "wb", buffering=0)
                    break
                except OSError:
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise
                    time.sleep(.2)
            def persistent_command(pipe, command, process):
                if process.poll() is not None:
                    raise RuntimeError(f"Client exited with {process.returncode}")
                connection.write((command + "\n").encode("utf-8"))
                time.sleep(.4)
            send_command = persistent_command
            send_command(pipe, "echo settings_tooltip_verify_ready", process)
            time.sleep(4)
            hwnd = focus(process)
            if not hwnd:
                raise RuntimeError("Client has no visible window")
            press_key(0x53)
            time.sleep(1)
            # General settings have immediate help for config rows. Dwell on
            # each row to exercise first container creation and cached updates.
            for x, y in [(160, 180), (160, 230), (160, 280), (160, 330), (160, 380), (500, 210)]:
                point(hwnd, x, y)
                time.sleep(.85)
                if process.poll() is not None:
                    raise RuntimeError(f"Client exited on settings tooltip: {process.returncode}")
            result["screenshots"]["general_tooltip"] = capture(process, pipe, root, "general_tooltip")
            for page in range(16):
                send_command(pipe, f"ui_settings_page {page}", process)
                point(hwnd, 220, 220)
                time.sleep(.65)
                if process.poll() is not None:
                    raise RuntimeError(f"Client exited on page {page}: {process.returncode}")
                result["pages"].append(page)
            result["screenshots"]["last_settings"] = capture(process, pipe, root, "last_settings")
            # A font/UI resize invalidates text caches; revisit the tooltip.
            send_command(pipe, "qm_ui_scale 125", process)
            send_command(pipe, "ui_settings_page 1", process)
            point(hwnd, 160, 260)
            time.sleep(1)
            result["screenshots"]["scaled_settings"] = capture(process, pipe, root, "scaled_settings")
            send_command(pipe, "qm_ui_scale 100", process)
            send_command(pipe, "cl_editor 1", process)
            time.sleep(2)
            result["screenshots"]["editor"] = capture(process, pipe, root, "editor")
            send_command(pipe, "cl_editor 0", process)
            time.sleep(1)
            send_command(pipe, "toggle_local_console", process)
            time.sleep(1)
            result["screenshots"]["console"] = capture(process, pipe, root, "console")
            send_command(pipe, "quit", process)
            process.wait(timeout=15)
            result["exit_code"] = process.returncode
            if process.returncode:
                raise RuntimeError(f"Client exited with {process.returncode}")
        except Exception:
            # Capture the failing process before terminating this test instance.
            debugger = Path(__file__).resolve().parents[2] / ".build-tools/w64devkit/bin/gdb.exe"
            try:
                trace = subprocess.run([str(debugger), "-batch", "-ex", "set pagination off", "-ex", "thread apply all bt", "-p", str(process.pid)], capture_output=True, text=True, timeout=20)
                (root / "debugger.log").write_text(trace.stdout + trace.stderr, encoding="utf-8")
            except (OSError, subprocess.TimeoutExpired):
                pass
            raise
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            if "connection" in locals():
                connection.close()
    text = (root / "client.log").read_text(encoding="utf-8", errors="replace")
    result["assertions"] = [line for line in text.splitlines() if "assert" in line.lower()]
    result["unknown_commands"] = [line for line in text.splitlines() if "No such command" in line]
    (root / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    if result["assertions"] or result["unknown_commands"]:
        raise RuntimeError("Inspect client log for verification errors")


if __name__ == "__main__":
    main()
