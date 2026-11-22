#!/usr/bin/env python3
"""Isolated native GUI smoke test. Never opens tablet/uinput/driver IPC."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import time


def run(command, env, **kwargs):
    return subprocess.run(command, env=env, check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--output", default="/tmp/opencode/osupoint-gui-smoke")
    args = parser.parse_args()
    for tool in ("Xvfb", "xdotool", "import"):
        if not shutil.which(tool):
            raise SystemExit(f"UNVERIFIED: GUI smoke requires {tool}")
    binary = str(Path(args.binary).resolve())
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    rfd, wfd = os.pipe()
    server = subprocess.Popen(
        ["Xvfb", "-displayfd", str(wfd), "-screen", "0", "2200x1800x24", "-nolisten", "tcp"],
        pass_fds=[wfd], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
    )
    os.close(wfd)
    display = os.read(rfd, 32).decode().strip()
    os.close(rfd)
    env = dict(os.environ, DISPLAY=f":{display}", LIBGL_ALWAYS_SOFTWARE="1", LC_ALL="C")
    env.pop("vblank_mode", None)
    env.pop("WAYLAND_DISPLAY", None)
    app = None
    try:
        cfg = output / "preview.ini"
        cfg.write_text("width=51\nheight=36.56\ncenter_x=70\ncenter_y=60\nrotation=-3\n"
                       "filter=0\nmotion_filter=0\nextrapolation_mm=0\ndebug_mode=0\nactive_tab=0\n")
        app = subprocess.Popen([binary, "--platform", "x11", "--preview", "--config", str(cfg), "--run-for", "15"],
                               cwd=output, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        window = None
        for _ in range(60):
            if app.poll() is not None:
                raise RuntimeError(app.communicate()[1].decode())
            found = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^osu!Point$"],
                                   env=env, text=True, capture_output=True)
            if found.returncode == 0:
                window = found.stdout.splitlines()[0]
                break
            time.sleep(0.1)
        if not window:
            raise RuntimeError("GUI window was not created")
        run(["xdotool", "windowfocus", "--sync", window], env)
        time.sleep(0.5)
        run(["import", "-window", window, str(output / "area.png")], env)
        run(["xdotool", "mousemove", "--window", window, "112", "118", "click", "1"], env)
        time.sleep(0.5)
        if "active_tab=1" not in cfg.read_text():
            raise AssertionError("Filters tab did not activate/save")
        run(["import", "-window", window, str(output / "filters.png")], env)
        run(["xdotool", "key", "F12"], env)
        time.sleep(1.2)
        if "debug_mode=1" not in cfg.read_text():
            raise AssertionError("F12 did not enable diagnostics")
        run(["import", "-window", window, str(output / "diagnostics.png")], env)
        run(["xdotool", "key", "F12"], env)
        time.sleep(0.6)
        if "debug_mode=0" not in cfg.read_text():
            raise AssertionError("F12 did not disable diagnostics")
        run(["xdotool", "windowsize", window, "680", "500"], env)
        time.sleep(0.5)
        run(["import", "-window", window, str(output / "compact.png")], env)
        app.terminate()
        stdout, stderr = app.communicate(timeout=5)
        if app.returncode != 0 or stdout or stderr:
            raise AssertionError(f"GUI exit={app.returncode}, stdout={stdout!r}, stderr={stderr!r}")
        if (output / "imgui.ini").exists() or (output / "imgui_log.txt").exists():
            raise AssertionError("GUI left automatic layout/log artifacts")
        print("PASS: native GUI, Filters tab, F12 On-Off, compact layout, clean exit, no automatic logs")
        # Verify a separately loaded light theme without relying on widget coordinates.
        light = output / "light.ini"
        light.write_text("theme_base=light\ndebug_mode=0\n")
        app = subprocess.Popen([binary, "--platform", "x11", "--preview", "--config", str(light), "--run-for", "2"],
                               cwd=output, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(0.7)
        window = run(["xdotool", "search", "--onlyvisible", "--name", "^osu!Point$"], env,
                     text=True, capture_output=True).stdout.splitlines()[0]
        run(["import", "-window", window, str(output / "light.png")], env)
        stdout, stderr = app.communicate(timeout=6)
        if app.returncode or stdout or stderr:
            raise AssertionError(f"Light-theme GUI: {app.returncode}, {stderr!r}")
        print(f"PASS: light theme; screenshots: {output}")
    finally:
        if app and app.poll() is None:
            app.terminate()
            app.wait(timeout=5)
        server.terminate()
        server.wait(timeout=5)


if __name__ == "__main__":
    main()
