#!/usr/bin/env python3
"""Linux desktop integration test for the WackGet window: the StatusNotifier tray icon and its
menu, the X11 hot key, `wackget --toggle`, and the .wgq association made from Preferences.

Needs an X server (it starts openbox on it), a private session bus and these tools:
openbox, xdotool, xdg-utils, desktop-file-utils, python3-gi, libayatana-appindicator3-1. Run as
    dbus-run-session -- xvfb-run -a -s "-screen 0 1280x800x24" python3 tests/linux_desktop.py build/wackget
Everything happens in a throwaway HOME; exits non-zero if a check fails."""
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
failures = []


def check(what, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + what + (f" ({detail})" if detail and not ok else ""), flush=True)
    if not ok:
        failures.append(what)


def run(*cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=30, **kw)


class Watcher:
    """tests/fake_sni_watcher.py, talked to over its stdin/stdout"""

    def __init__(self, env):
        self.p = subprocess.Popen([sys.executable, os.path.join(HERE, "fake_sni_watcher.py")], env=env,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.lines = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()
        self.wait_for("WATCHER UP")

    def _read(self):
        for line in self.p.stdout:
            self.lines.put(line.rstrip("\n"))

    def wait_for(self, prefix, timeout=10):
        end = time.time() + timeout
        while time.time() < end:
            try:
                line = self.lines.get(timeout=max(0.1, end - time.time()))
            except queue.Empty:
                break
            if line.startswith(prefix):
                return line
        return None

    def send(self, cmd):
        self.p.stdin.write(cmd + "\n")
        self.p.stdin.flush()

    def dump(self):
        self.send("dump")
        props, menu = self.wait_for("PROPS"), self.wait_for("MENU")
        return props or "", menu or ""

    def click(self, label):
        self.send("click " + label)
        return self.wait_for("CLICKED") is not None

    def stop(self):
        self.p.terminate()


def visible():
    r = run("xdotool", "search", "--onlyvisible", "--name", "^WackGet$")
    return len(r.stdout.split())


def wait_visible(want, timeout=5):
    end = time.time() + timeout
    while time.time() < end:
        if (visible() > 0) == want:
            return True
        time.sleep(0.2)
    return False


def main():
    exe = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp(prefix="wg-desktop-")
    env = dict(os.environ)
    env["HOME"] = os.path.join(tmp, "home")
    env["XDG_RUNTIME_DIR"] = os.path.join(tmp, "run")
    for k in ("XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CURRENT_DESKTOP"):
        env.pop(k, None)
    os.makedirs(env["HOME"])
    os.makedirs(env["XDG_RUNTIME_DIR"], mode=0o700)
    cfg = os.path.join(tmp, "wackget.ini")
    with open(cfg, "w") as f:
        f.write(f"DownloadDir={os.path.join(tmp, 'dl')}\nHotKey=1\n")

    wm = subprocess.Popen(["openbox"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    watcher = Watcher(env)
    app = subprocess.Popen([exe, f"--config={cfg}"], env=env)
    try:
        # Tray: registers with the panel, with the original tray menu plus Show/Hide
        reg = watcher.wait_for("REGISTERED", 15)
        check("tray icon registers as a StatusNotifierItem", reg is not None)
        check("window shows at start", wait_visible(True))
        props, menu = watcher.dump()
        check("tray icon id, title and icon", "'Id': 'io.github.levoniust.WackGet'" in props and
              "'IconName': 'wackget-idle'" in props and "downloading" in props, props)
        for label in ("Hide WackGet", "Add _New Item...", "Add From _Clipboard", "_Preferences...",
                      "Open Download _Folder", "E_xit"):
            check(f"tray menu has {label!r}", f"'{label}'" in menu, menu)
        icon_dir = os.path.join(env["XDG_RUNTIME_DIR"], "wackget-tray")
        check("tray icons written", all(os.path.exists(os.path.join(icon_dir, f"wackget-{n}.png"))
                                       for n in ("idle", "busy")))
        watcher.click("Hide WackGet")
        check("tray menu: Hide hides the window", wait_visible(False))
        time.sleep(1.5)  # the label follows once a second
        _, menu = watcher.dump()
        check("tray menu now says Show", "'Show WackGet'" in menu, menu)
        watcher.click("Show WackGet")
        check("tray menu: Show brings it back", wait_visible(True))

        # Hot key (X11 grab of Ctrl+Alt+W): shows when hidden, hides when in front
        watcher.click("Hide WackGet")
        wait_visible(False)
        run("xdotool", "key", "ctrl+alt+w", env=env)
        check("hot key shows the hidden window", wait_visible(True))
        run("xdotool", "search", "--onlyvisible", "--name", "^WackGet$", "windowactivate", "--sync", env=env)
        time.sleep(0.5)
        run("xdotool", "key", "ctrl+alt+w", env=env)
        check("hot key hides the window in front", wait_visible(False))

        # wackget --toggle goes to the running one
        r = run(exe, f"--config={cfg}", "--toggle", env=env)
        check("--toggle returns at once", r.returncode == 0, r.stderr)
        check("--toggle shows it", wait_visible(True))

        # .wgq association from Preferences > Integration
        def prefs_toggle_assoc():
            run("xdotool", "search", "--onlyvisible", "--name", "^WackGet$", "windowactivate", "--sync", env=env)
            for keys, pause in ((["alt+f"], 0.5), (["p"], 1.5), (["ctrl+Page_Down"], 0.5), (["alt+s"], 0.3),
                                (["Return"], 2)):
                run("xdotool", "key", *keys, env=env)
                time.sleep(pause)

        mime = "application/x-wackget-queue"
        prefs_toggle_assoc()
        r = run("xdg-mime", "query", "default", mime, env=env)
        check("association: WackGet is the default for .wgq", r.stdout.strip() == "io.github.levoniust.WackGet.desktop",
              r.stdout + r.stderr)
        sample = os.path.join(tmp, "x.wgq")
        open(sample, "w").close()
        r = run("xdg-mime", "query", "filetype", sample, env=env)
        check("association: .wgq is a WackGet queue", r.stdout.strip() == mime, r.stdout + r.stderr)
        desktop = os.path.join(env["HOME"], ".local/share/applications/io.github.levoniust.WackGet.desktop")
        if os.path.exists("/usr/share/applications/io.github.levoniust.WackGet.desktop"):
            check("association: uses the installed launcher", not os.path.exists(desktop))
        else:
            r = run("desktop-file-validate", desktop)
            check("association: launcher written and valid", os.path.exists(desktop) and r.returncode == 0,
                  r.stdout + r.stderr)
        prefs_toggle_assoc()
        r = run("xdg-mime", "query", "default", mime, env=env)
        check("association: switched off again", r.stdout.strip() == "", r.stdout)

        # Exit from the tray menu (empty queue: no question)
        watcher.click("Exit")
        try:
            app.wait(10)
            check("tray menu: Exit quits", True)
        except subprocess.TimeoutExpired:
            check("tray menu: Exit quits", False)
    finally:
        if app.poll() is None:
            app.kill()
        watcher.stop()
        wm.terminate()
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"{len(failures)} failed" if failures else "all passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
