"""Exercise the native demo editor in an isolated, non-saving client profile."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

from verify_video_background import send_command
from verify_xyrion_features import focus


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--demo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ffprobe', type=Path, required=True)
    parser.add_argument('--ffmpeg', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    demo = args.demo.resolve()
    before = hashlib.sha256(demo.read_bytes()).hexdigest()
    pipe = f'xyrion_demo_editor_{int(time.time())}'
    (output / 'storage.cfg').write_text('add_path $CURRENTDIR\nadd_path $DATADIR\n', encoding='utf-8')
    cfg = output / 'test.cfg'
    cfg.write_text(
        'cl_save_settings 0\ncl_show_welcome 0\ncl_menu_map ""\n'
        'qm_steam_auto_launch 0\nxc_video_enabled 0\n'
        'gfx_fullscreen 0\ngfx_screen_width 960\ngfx_screen_height 600\n'
        'gfx_gl_major 3\ngfx_gl_minor 3\ngfx_vsync 0\n'
        'ui_mousesens 100\n'
        'cl_refresh_rate 120\ncl_refresh_rate_inactive 120\nsnd_enable 1\n'
        'cl_video_recorder_fps 30\ncl_video_crf 28\ncl_video_preset 0\n'
        f'cl_input_fifo {pipe}\n', encoding='utf-8')
    result = {}
    process = None
    with (output / 'client.log').open('wb') as log:
        try:
            process = subprocess.Popen([str(args.client.resolve()), '-f', str(cfg)], cwd=output, stdout=log, stderr=subprocess.STDOUT)

            def text():
                return (output / 'client.log').read_text(encoding='utf-8', errors='replace')

            def cmd(command):
                send_command(pipe, command, process)

            def wait(check, label, seconds=45):
                deadline = time.monotonic() + seconds
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f'Client exited ({process.returncode}): {label}')
                    if check():
                        return
                    time.sleep(.1)
                raise RuntimeError(f'Timeout: {label}; inspect {output / "client.log"}')

            def shot(name):
                old = set((output / 'screenshots').glob('*.png'))
                cmd('screenshot')
                wait(lambda: bool(set((output / 'screenshots').glob('*.png')) - old), name)
                path = max((output / 'screenshots').glob('*.png'), key=lambda p: p.stat().st_mtime)
                result[name] = str(path)

            cmd('echo demo_editor_test_ready')
            user = ctypes.windll.user32
            user.SetProcessDPIAware()
            windows = []
            user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
            user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]

            @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            def enum_window(hwnd, _):
                pid = wintypes.DWORD()
                user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                cls = ctypes.create_unicode_buffer(128)
                user.GetClassNameW(hwnd, cls, 128)
                if pid.value == process.pid and 'SDL' in cls.value:
                    windows.append(hwnd)
                return True

            user.EnumWindows(enum_window, 0)
            assert windows, 'No SDL client window'
            focus(process)
            mouse_pos = [0, 0]
            user.mouse_event(1, -10000, -10000, 0, 0)
            time.sleep(.2)

            def pointer(x, y):
                # SDL uses raw relative motion; SetCursorPos does not supply it.
                user.mouse_event(1, x - mouse_pos[0], y - mouse_pos[1], 0, 0)
                mouse_pos[:] = [x, y]
                time.sleep(.12)

            def drag(x1, y1, x2, y2):
                focus(process)
                pointer(x1, y1)
                user.mouse_event(2, 0, 0, 0, 0)
                time.sleep(.15)
                pointer(x2, y2)
                user.mouse_event(4, 0, 0, 0, 0)
                time.sleep(.2)
            cmd('play ' + demo.as_posix())
            cmd('toggle_demo_editor')
            cmd('demo_editor_name regression')
            for action in ('add 0 100', 'add 250 450', 'speed 200', 'select 0', 'split 50', 'select 2', 'move 0', 'undo', 'redo', 'save'):
                cmd('demo_editor ' + action)
            project_path = output / 'demo-projects/regression.xyrion-cut.json'
            wait(project_path.exists, 'project save')
            project = json.loads(project_path.read_text(encoding='utf-8'))
            first = project['first_tick']
            expected = [(first + 250, first + 450, 200), (first, first + 50, 100), (first + 50, first + 100, 100)]
            assert [(x['in'], x['out'], x['speed']) for x in project['clips']] == expected, project
            result['edit_order_split_speed_undo_redo'] = True
            cmd('demo_editor remove')
            cmd('demo_editor load')
            cmd('demo_editor save')
            assert json.loads(project_path.read_text(encoding='utf-8')) == project
            result['save_load'] = True
            drag(830, 495, 120, 495)
            cmd('demo_editor save')
            dragged = json.loads(project_path.read_text(encoding='utf-8'))
            assert dragged['clips'][0] == project['clips'][2], dragged
            cmd('demo_editor undo')
            # Select the first clip, then drag its right trim handle.
            drag(250, 495, 250, 495)
            drag(476, 495, 447, 495)
            cmd('demo_editor save')
            trimmed = json.loads(project_path.read_text(encoding='utf-8'))
            assert trimmed['clips'][0]['out'] < project['clips'][0]['out'], trimmed
            cmd('demo_editor undo')
            cmd('demo_editor save')
            assert json.loads(project_path.read_text(encoding='utf-8')) == project
            result['mouse_reorder_and_trim'] = True
            shot('timeline')
            cmd('demo_editor preview')
            time.sleep(1)
            cmd('demo_editor preview')
            cmd('demo_editor status')
            result['preview'] = True
            cmd('demo_editor demo')
            wait(lambda: (output / 'demos/edits/regression/clip-03.demo').exists(), 'demo export')
            result['demo_clips'] = [str(p) for p in sorted((output / 'demos/edits/regression').glob('*.demo'))]
            assert len(result['demo_clips']) == 3
            cmd('demo_editor mp4')
            video = output / 'videos/regression.mp4'
            wait(video.exists, 'MP4 export', 90)

            def probe(path):
                return json.loads(subprocess.check_output([str(args.ffprobe.resolve()), '-v', 'error', '-count_frames', '-show_streams', '-show_format', '-of', 'json', str(path)], text=True, encoding='utf-8'))

            info = probe(video)
            stream = next(x for x in info['streams'] if x['codec_type'] == 'video')
            assert stream['codec_name'] == 'h264', info
            assert int(stream['nb_read_frames']) == 120, info
            assert abs(float(stream['duration']) - 4) <= .002, info
            assert stream['width'] == 960 and stream['height'] == 600, info
            assert any(x['codec_type'] == 'audio' for x in info['streams']), info
            result['mp4'] = info
            original_video = hashlib.sha256(video.read_bytes()).hexdigest()
            cmd('demo_editor mp4')
            assert hashlib.sha256(video.read_bytes()).hexdigest() == original_video
            result['existing_video_not_overwritten'] = True
            subprocess.run([str(args.ffmpeg.resolve()), '-v', 'error', '-y', '-i', str(video), '-frames:v', '1', str(output / 'first-frame.png')], check=True)
            # Reopen each exported demo and inspect its actual source tick range.
            actual = []
            for i, path in enumerate(result['demo_clips']):
                cmd('play ' + Path(path).as_posix())
                cmd('toggle_demo_editor')
                cmd(f'demo_editor_name clip-check-{i}')
                cmd('demo_editor save')
                saved = json.loads((output / f'demo-projects/clip-check-{i}.xyrion-cut.json').read_text(encoding='utf-8'))
                actual.append((saved['first_tick'], saved['last_tick']))
            # This fixture records snapshots every two ticks. Export includes
            # the snapshots inside each [in,out), with no duplicated split.
            assert all(a <= lo <= a + 1 and b - 2 <= hi < b for (lo, hi), (a, b, _) in zip(actual, expected)), (actual, expected)
            result['demo_ranges'] = actual
            # Tail of source followed by a backwards cut, testing EOF safety.
            cmd('play ' + demo.as_posix())
            cmd('toggle_demo_editor')
            cmd('demo_editor_name eof-regression')
            last = project['last_tick'] - first
            for action in (f'add {last - 100} {last}', 'add 100 200', 'mp4'):
                cmd('demo_editor ' + action)
            eof = output / 'videos/eof-regression.mp4'
            wait(eof.exists, 'EOF followed by backwards cut', 90)
            tail = probe(eof)
            stream = next(x for x in tail['streams'] if x['codec_type'] == 'video')
            assert int(stream['nb_read_frames']) == 120, tail
            assert abs(float(stream['duration']) - 4) <= .002, tail
            result['eof_then_backwards'] = True
            # A one-tick tail is valid video material but cannot contain two
            # snapshots for a playable .demo. Test both output paths.
            for action in ('remove', 'select 0', 'remove', f'add {last - 1} {last}', 'add 100 200'):
                cmd('demo_editor ' + action)
            cmd('demo_editor_name tiny-regression')
            cmd('demo_editor demo')
            wait(lambda: '两个快照' in text(), 'too-short Demo rejected')
            assert not list((output / 'demos/edits/tiny-regression').glob('*.demo'))
            result['short_demo_rejected'] = True
            cmd('cl_video_recorder_fps 60')
            cmd('demo_editor mp4')
            tiny = output / 'videos/tiny-regression.mp4'
            wait(tiny.exists, 'first clip at EOF / 60 FPS', 90)
            tiny_info = probe(tiny)
            stream = next(x for x in tiny_info['streams'] if x['codec_type'] == 'video')
            assert int(stream['nb_read_frames']) == 122, tiny_info
            assert abs(float(stream['duration']) - 122 / 60) <= .002, tiny_info
            result['tiny_tail_at_60fps'] = True
            cmd('demo_editor_name cancel-regression')
            cmd(f'demo_editor add 0 {last}')
            cmd('demo_editor speed 25')
            cmd('demo_editor mp4')
            cmd('demo_editor cancel')
            wait(lambda: (output / 'videos/cancel-regression_rendering.mp4').exists(), 'cancelled partial video')
            cmd('demo_editor status')
            assert not (output / 'videos/cancel-regression.mp4').exists()
            result['cancel_preserves_partial'] = True
            assert 'No such command' not in text()
            cmd('quit')
            process.wait(timeout=15)
            assert process.returncode == 0
            result['exit_code'] = process.returncode
            assert hashlib.sha256(demo.read_bytes()).hexdigest() == before
            result['source_unchanged'] = True
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            (output / 'result.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
