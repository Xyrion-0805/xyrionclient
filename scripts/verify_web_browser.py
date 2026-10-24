"""Exercise the native browser and fullscreen/video lifecycle in an isolated profile."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build-xyrion" / "browser-oct17-smoke"
OUT.mkdir(exist_ok=True)
(OUT / "storage.cfg").write_text("add_path $CURRENTDIR\nadd_path $DATADIR\n", encoding="utf-8")
PIPE = "xyrion_browser_oct17_smoke"
(OUT / "client.cfg").write_text(
    "cl_save_settings 0\ncl_show_welcome 0\ncl_menu_map \"\"\nsnd_enable 0\n"
    "gfx_fullscreen 0\ngfx_screen_width 1280\ngfx_screen_height 800\ngfx_vsync 1\n"
    "cl_refresh_rate 60\ncl_refresh_rate_inactive 60\ngfx_gl_major 3\ngfx_gl_minor 0\n"
    "xc_video_enabled 1\nxc_browser_url https://example.com\n"
    f'xc_video_path "{(ROOT.parent / "redpandacompress_59.97.mp4").as_posix()}"\n'
    f"cl_input_fifo {PIPE}\n", encoding="utf-8")
U = ctypes.windll.user32
U.FindWindowW.restype = wintypes.HWND
U.FindWindowExW.restype = wintypes.HWND
U.GetForegroundWindow.restype = wintypes.HWND
for name in ["GetWindowRect", "GetClientRect"]:
    getattr(U, name).argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
U.SetForegroundWindow.argtypes = [wintypes.HWND]
U.SetFocus.argtypes = [wintypes.HWND]
U.IsWindowVisible.argtypes = [wintypes.HWND]
U.IsIconic.argtypes = [wintypes.HWND]
U.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
U.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
U.MoveWindow.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.BOOL]
U.SetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPCWSTR]
U.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
U.EnumChildWindows.argtypes = [wintypes.HWND, ctypes.c_void_p, wintypes.LPARAM]
U.EnumWindows.argtypes = [ctypes.c_void_p, wintypes.LPARAM]
U.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
U.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]


def command(text, process):
    deadline = time.monotonic() + 18
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"client exited: {process.returncode}")
        try:
            with open("\\\\.\\pipe\\" + PIPE, "wb", buffering=0) as f:
                f.write((text + "\n").encode())
                time.sleep(1.5)
            return
        except OSError:
            time.sleep(.2)
    raise RuntimeError("command pipe timeout")


def windows(parent=None):
    result = []
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def callback(hwnd, _):
        pid = wintypes.DWORD()
        U.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        cls = ctypes.create_unicode_buffer(256)
        U.GetClassNameW(hwnd, cls, 256)
        result.append((hwnd, pid.value, cls.value))
        return True
    if parent:
        U.EnumChildWindows(parent, callback, 0)
    else:
        U.EnumWindows(callback, 0)
    return result


def snapshot(hwnd, name):
    rect = wintypes.RECT()
    U.GetWindowRect(hwnd, ctypes.byref(rect))
    path = str(OUT / (name + ".png")).replace("'", "''")
    script = (
        "Add-Type -AssemblyName System.Drawing; "
        f"$b = New-Object System.Drawing.Bitmap({rect.right-rect.left},{rect.bottom-rect.top}); "
        "$g = [System.Drawing.Graphics]::FromImage($b); "
        f"$g.CopyFromScreen({rect.left},{rect.top},0,0,$b.Size); "
        f"$b.Save('{path}'); $g.Dispose(); $b.Dispose()"
    )
    subprocess.run(["powershell.exe", "-NoProfile", "-Command", script], check=True, creationflags=0x08000000)


def main():
    result = {}
    with (OUT / "client.log").open("wb") as log:
        process = subprocess.Popen([str(ROOT / "build-xyrion/XyrionClient-update.exe"), "-f", str(OUT / "client.cfg")], cwd=OUT, stdout=log, stderr=subprocess.STDOUT)
        try:
            command("echo browser_test_ready", process)
            time.sleep(5)
            hwnd = next(h for h,p,c in windows() if p == process.pid and c.startswith("SDL"))
            U.SetForegroundWindow(hwnd)
            command("toggle_browser", process)
            time.sleep(12)
            browser = U.FindWindowExW(hwnd, None, "XyrionWebBrowser", None)
            assert browser and U.IsWindowVisible(browser), "browser was not visible"
            result["browser_children"] = [(p,c) for _,p,c in windows(browser)]
            assert any("Chrome" in c for _,_,c in windows(browser)), "WebView2 did not create its window"
            snapshot(hwnd, "browser_open")
            U.MoveWindow(browser, 150, 90, 800, 500, True)
            time.sleep(1)
            snapshot(hwnd, "browser_moved_resized")
            command("xc_browser_x", process); command("xc_browser_w", process)
            U.ShowWindow(browser, 6)
            assert U.IsIconic(browser), "minimize failed"
            U.ShowWindow(browser, 9)
            time.sleep(1)
            U.PostMessageW(browser, 0x0010, 0, 0)
            time.sleep(2)
            assert not U.IsWindowVisible(browser), "close did not hide browser"
            command("gfx_fullscreen 1", process)
            time.sleep(2)
            command("toggle_browser", process)
            time.sleep(2)
            command("gfx_fullscreen", process)
            snapshot(hwnd, "browser_from_fullscreen")
            command("toggle_browser", process)
            time.sleep(2)
            command("gfx_fullscreen", process)
            for mode, width, height in [(1, 1280, 720), (1, 1920, 1080), (2, 1280, 800), (0, 1001, 701), (1, 1280, 720), (0, 1280, 800)]:
                command(f"gfx_fullscreen {mode}", process)
                command(f"gfx_screen_width {width}", process)
                command(f"gfx_screen_height {height}", process)
                time.sleep(2)
                assert process.poll() is None
            result["resolution_changes"] = 6
            command("quit", process)
            process.wait(timeout=15)
            result["exit_code"] = process.returncode
            assert process.returncode == 0
        except Exception as error:
            result["error"] = str(error)
            raise
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            (OUT / "result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
            print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
