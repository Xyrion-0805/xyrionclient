"""Exercise console rendering and AI transport failures using local-only profiles."""
import argparse
import ctypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import subprocess
import threading
import time

from verify_three_dummies import Pipe, capture_window
from verify_xyrion_features import focus


class MockApi(BaseHTTPRequestHandler):
    requests = []
    lock = threading.Lock()

    def log_message(self, *_):
        pass

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        with self.lock:
            self.requests.append(self.path)
        if self.path == "/drop":
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close()
            return
        if self.path == "/slow":
            time.sleep(4)
        code = int(self.path[1:]) if self.path[1:].isdigit() else 200
        body = b'{"choices":[{"message":{"content":"local regression reply"}}]}'
        if self.path == "/invalid":
            body = b"invalid json"
        elif self.path == "/oversize":
            body = b"x" * (1024 * 1024 + 1)
        try:
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (OSError, ConnectionError):
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--transport-only", action="store_true")
    parser.add_argument("--debugger", type=Path)
    parser.add_argument("--gdb-breakpoint", default="abort")
    parser.add_argument("--stress-cycles", type=int, default=0)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    client_exe, server_exe = args.client.resolve(), args.server.resolve()
    processes, pipes, logs = [], [], []
    result = {"checks": [], "client": str(client_exe)}
    mock = ThreadingHTTPServer(("127.0.0.1", 0), MockApi)
    mock.daemon_threads = True
    threading.Thread(target=mock.serve_forever, daemon=True).start()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]

    def start(exe, name, config, server=False):
        profile = root / name
        profile.mkdir(exist_ok=True)
        (profile / "storage.cfg").write_text("add_path $CURRENTDIR\nadd_path $DATADIR\n", encoding="utf-8")
        if server:
            (profile / "autoexec_server.cfg").write_text("", encoding="utf-8")
        pipe_name = f"xyrion_console_{name}_{os.getpid()}"
        cfg = profile / "test.cfg"
        cfg.write_text(config + f"\n{'sv' if server else 'cl'}_input_fifo {pipe_name}\n", encoding="utf-8")
        log = (profile / "output.log").open("wb")
        logs.append(log)
        process = subprocess.Popen([str(exe), "-f", str(cfg)], cwd=profile, stdout=log, stderr=subprocess.STDOUT)
        processes.append(process)
        result.setdefault("processes", {})[name] = process.pid
        pipe = Pipe(pipe_name, process)
        pipes.append(pipe)
        return process, pipe, profile

    def alive():
        for process in processes:
            if process.poll() is not None:
                raise RuntimeError(f"Test process {process.pid} exited: {process.returncode}")

    def wait_for(predicate, seconds=15):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            alive()
            if predicate():
                return
            time.sleep(.1)
        raise TimeoutError("Local regression condition timed out")

    def count(path):
        with MockApi.lock:
            return MockApi.requests.count(path)

    def checkpoint(name):
        # Use console-only status output: echo is overridden to print in chat.
        path = main_profile / "output.log"
        offset = path.stat().st_size
        cp.send("dummy_status")
        wait_for(lambda: "connected=0/3" in path.read_bytes()[offset:].decode("utf-8", errors="replace"))
        result["checks"].append(name)
        (root / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")

    def screenshot(name):
        image = Path(capture_window(client, root))
        image.replace(root / f"{name}.png")

    def console_cycles(label):
        for command in ("toggle_local_console", "toggle_remote_console"):
            for _ in range(5):
                cp.send(command)
                time.sleep(.35)
                alive()
                cp.send(command)
                time.sleep(.35)
        cp.send("toggle_local_console")
        time.sleep(.6)
        for size in (1, 24, 10):
            cp.send(f"qm_console_font_size {size}")
            time.sleep(.4)
            cp.send("console_page_top")
            cp.send("console_page_bottom")
            alive()
        screenshot(f"{label}-console")
        cp.send("toggle_local_console")
        time.sleep(.4)
        # Exercise the actual F1 binding as well as the command path.
        focus(client)
        for _ in range(4):
            ctypes.windll.user32.keybd_event(0, 0x3B, 8, 0)
            time.sleep(.08)
            ctypes.windll.user32.keybd_event(0, 0x3B, 10, 0)
            time.sleep(.4)
            alive()
        checkpoint(f"{label}: local/remote console cycles, font rebuild, F1")

    base_config = (
        'cl_save_settings 0\ncl_show_welcome 0\ncl_skip_start_menu 1\ncl_menu_map ""\n'
        'snd_enable 0\ngfx_fullscreen 0\ngfx_vsync 1\ngfx_screen_width 960\ngfx_screen_height 640\n'
        'cl_refresh_rate 60\ncl_refresh_rate_inactive 60\ncl_auto_demo_record 0\ncl_auto_race_record 0\n'
        'gfx_gl_major 3\ngfx_gl_minor 3\nxc_video_enabled 0\nqm_steam_auto_launch 0\n'
        'cl_languagefile ""\nhttp_allow_insecure 1\nbind f1 toggle_local_console\n'
    )
    try:
        server, sp, server_profile = start(server_exe, "server",
            f'bindaddr 127.0.0.1\nsv_port {port}\nsv_register 0\nsv_sixup 0\n'
            'sv_map "Sunny Side Up"\nsv_max_clients_per_ip 4\nsv_spamprotection 0\n', True)
        client, cp, main_profile = start(client_exe, "main", base_config + 'player_name CrashConsole\n')
        time.sleep(2)
        if not args.transport_only:
            console_cycles("menu")
        cp.send(f"connect 127.0.0.1:{port}")
        peer, pp, peer_profile = start(client_exe, "peer", base_config + f'player_name LocalPeer\nconnect 127.0.0.1:{port}\n')
        server_log = server_profile / "output.log"
        wait_for(lambda: server_log.read_text(encoding="utf-8", errors="replace").count("player has entered the game.") >= 2)
        time.sleep(2)
        if not args.transport_only:
            console_cycles("online")

        cp.send("xc_ai_key local-test-key")
        cp.send("xc_ai_cooldown 0")
        cp.send("xc_ai_web_search 0")
        cp.send("xc_ai_enabled 1")
        if args.debugger:
            debug_log = (root / "debugger.log").open("wb")
            logs.append(debug_log)
            debugger = subprocess.Popen([str(args.debugger.resolve()), str(client_exe), "-batch", "-ex", "set pagination off",
                "-p", str(client.pid), "-ex", f"break {args.gdb_breakpoint}", "-ex", "continue", "-ex", "bt",
                "-ex", "info all-registers", "-ex", "detach"],
                stdout=debug_log, stderr=subprocess.STDOUT)
            time.sleep(3)
        for iteration in range(args.stress_cycles):
            cp.send("toggle_local_console")
            cp.send(f"qm_console_font_size {(1, 24, 10)[iteration % 3]}")
            time.sleep(.15)
            cp.send("toggle_local_console")
            time.sleep(.15)
            alive()
        if args.stress_cycles:
            checkpoint(f"console stress: {args.stress_cycles} cycles with font changes")
        for path, retries in (("drop", 2), ("401", 1), ("429", 2), ("500", 2), ("invalid", 1), ("oversize", 2), ("valid", 1)):
            cp.send(f"xc_ai_endpoint http://127.0.0.1:{mock.server_port}/{path}")
            time.sleep(.5)
            pp.send(f"say CrashConsole: local case {path}")
            wait_for(lambda: count("/" + path) >= 1)
            cp.send("toggle_local_console")
            if retries == 2:
                wait_for(lambda: count("/" + path) >= retries, 22)
            time.sleep(1)
            cp.send("toggle_local_console")
            checkpoint(f"AI {path}: {count('/' + path)} request(s), console responsive")
            if path == "valid":
                wait_for(lambda: "local regression reply" in (peer_profile / "output.log").read_text(encoding="utf-8", errors="replace"))
        cp.send(f"xc_ai_endpoint http://127.0.0.1:{mock.server_port}/slow")
        time.sleep(.5)
        pp.send("say CrashConsole: local cancellation case")
        wait_for(lambda: count("/slow") == 1)
        cp.send("xc_ai_enabled 0")
        time.sleep(5)
        checkpoint("AI cancellation: no crash or delayed chat reply")
        cp.send("disconnect")
        time.sleep(.5)
        checkpoint("disconnect after console and AI failures")
        for pipe in reversed(pipes[1:]):
            pipe.send("quit")
        sp.send("shutdown")
        for process in processes:
            process.wait(timeout=15)
            if process.returncode:
                raise RuntimeError(f"Exit code: {process.returncode}")
        result["exit_codes"] = [process.returncode for process in processes]
        for log_path in root.glob("*/output.log"):
            text = log_path.read_text(encoding="utf-8", errors="replace")
            errors = [line for line in text.splitlines() if "assert" in line.lower() or "No such command" in line]
            if errors:
                raise RuntimeError(f"Unexpected diagnostics in {log_path}: {errors}")
        result["passed"] = True
    except Exception as error:
        result["error"] = str(error)
        result["passed"] = False
        raise
    finally:
        if "debugger" in locals() and debugger.poll() is None:
            debugger.terminate()
            debugger.wait(timeout=10)
        for pipe in pipes:
            pipe.close()
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
        for log in logs:
            log.close()
        mock.shutdown()
        (root / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
