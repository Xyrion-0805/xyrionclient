"""Benchmark the actual menu player in an isolated, non-saving profile."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import time


def send_command(pipe_name, command, process):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Client exited early ({process.returncode})")
        try:
            with open("\\\\.\\pipe\\" + pipe_name, "wb", buffering=0) as pipe:
                pipe.write((command + "\n").encode("utf-8"))
                # The engine's nonblocking pipe discards messages if the
                # writer disconnects before its next update processes them.
                time.sleep(0.3)
            return
        except OSError:
            time.sleep(0.2)
    raise RuntimeError("Client command pipe was not available")


def quote(value):
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'


def percentile(samples, fraction):
    return samples[min(len(samples) - 1, max(0, math.ceil(len(samples) * fraction) - 1))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--video", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=12)
    parser.add_argument("--width", type=int, default=2560)
    parser.add_argument("--height", type=int, default=1600)
    parser.add_argument("--gl-major", type=int, default=3)
    parser.add_argument("--gl-minor", type=int, default=0)
    parser.add_argument("--disable-video", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "storage.cfg").write_text("add_path $CURRENTDIR\nadd_path $DATADIR\n", encoding="utf-8")
    pipe = f"xyrion_background_verify_{os.getpid()}"
    cfg = output / "verify.cfg"
    cfg.write_text(
        "cl_save_settings 0\ncl_show_welcome 0\ncl_skip_start_menu 0\ncl_menu_map \"\"\n"
        "snd_enable 0\ngfx_fullscreen 0\ngfx_vsync 0\ncl_refresh_rate 448\n"
        "cl_refresh_rate_inactive 448\ngfx_refresh_rate 448\ngfx_refresh_rate_background 448\n"
        f"gfx_screen_width {args.width}\ngfx_screen_height {args.height}\n"
        f"gfx_gl_major {args.gl_major}\ngfx_gl_minor {args.gl_minor}\n"
        f"cl_input_fifo {pipe}\nxc_video_enabled {int(not args.disable_video)}\n"
        f"xc_video_path {quote(args.video.resolve())}\n", encoding="utf-8")
    frames = output / "frametimes.txt"
    log_path = output / "client.log"
    with log_path.open("wb") as log:
        process = subprocess.Popen([str(args.client.resolve()), "-f", str(cfg)], cwd=output,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            send_command(pipe, "echo video_verification_ready", process)
            time.sleep(8)
            send_command(pipe, "screenshot", process)
            send_command(pipe, f"benchmark_quit {args.seconds} {quote(frames)}", process)
            process.wait(timeout=args.seconds + 25)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    text = log_path.read_text(encoding="utf-8", errors="replace")
    if process.returncode != 0:
        raise RuntimeError(f"Client exited with {process.returncode}; inspect {log_path}")
    samples = sorted(int(x) for x in re.findall(r"Frametime (\d+) us", frames.read_text()))
    if not samples:
        raise RuntimeError("No frame-time samples")
    mean = statistics.mean(samples)
    result = {"client": str(args.client.resolve()), "video": str(args.video.resolve()),
              "exit_code": process.returncode, "samples": len(samples),
              "average_fps": round(1_000_000 / mean, 2), "mean_ms": round(mean / 1000, 3),
              "p95_ms": percentile(samples, 0.95) / 1000,
              "p99_ms": percentile(samples, 0.99) / 1000, "max_ms": samples[-1] / 1000,
              "frames_over_16_7_ms": sum(x > 16667 for x in samples),
              "video_logs": [x for x in text.splitlines() if "video_background" in x],
              "screenshots": [str(x) for x in (output / "screenshots").glob("*.png")]}
    (output / "result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
