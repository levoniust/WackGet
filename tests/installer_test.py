#!/usr/bin/env python3
"""Windows setup, used like a person would: click through every page (screenshots of each), then
install over it again (upgrade), and try to install while WackGet is running (it must ask and,
when nobody answers, stop without breaking anything). Leaves WackGet installed for the next CI
steps. Usage: installer_test.py WackGet-x.y.z-setup.exe OUTDIR"""
import os
import subprocess
import sys
import tempfile
import time
import winreg

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from edge_test import check, failures, user32, wait_for, wintypes  # noqa: E402
import ctypes  # noqa: E402

DIR = os.path.join(os.environ["LOCALAPPDATA"], "Programs", "WackGet")
EXE = os.path.join(DIR, "WackGet.exe")
UNINST = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\WackGet"


def setup_windows(pid):
    """Top-level windows of the setup process with their titles"""
    found = []
    proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def cb(hwnd, _):
        p = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, buf, 256)
        if p.value == pid and user32.IsWindowVisible(hwnd) and buf.value:
            found.append((hwnd, buf.value))
        return True
    user32.EnumWindows(proc(cb), 0)
    return found


def shot(hwnd, path):
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    ps = ("Add-Type -AssemblyName System.Drawing;"
          f"$b=New-Object System.Drawing.Bitmap {w},{h};$g=[System.Drawing.Graphics]::FromImage($b);"
          f"$g.CopyFromScreen({r.left},{r.top},0,0,$b.Size);$b.Save('{path}')")
    subprocess.run(["powershell", "-NoProfile", "-Command", ps], check=False)


def press_enter(hwnd):
    """The page's default button (I Agree / Next / Install / Close, id IDOK), clicked by id: CI
    desktops don't deliver typed keys"""
    user32.PostMessageW(hwnd, 0x0111, 1, 0)  # WM_COMMAND IDOK


def uninstall_entries():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINST) as k:
            return winreg.QueryValueEx(k, "DisplayVersion")[0]
    except OSError:
        return None


def main():
    setup, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    os.makedirs(out, exist_ok=True)

    # 1. Click through: licence (I Agree), components (Next), folder (Install), done (Close)
    p = subprocess.Popen([setup])
    pages = []
    for step in range(4):
        title_ok = wait_for(lambda: setup_windows(p.pid), 20)
        if not title_ok:
            break
        time.sleep(1.5)
        hwnd, title = setup_windows(p.pid)[0]
        pages.append(title)
        shot(hwnd, os.path.join(out, f"setup-{step + 1}.png"))
        if step == 3:
            # the last page waits for the files; Close is enabled once done
            wait_for(lambda: os.path.exists(EXE), 60)
            time.sleep(1)
            shot(hwnd, os.path.join(out, "setup-4.png"))
        press_enter(hwnd)
        time.sleep(1.5)
    try:
        rc = p.wait(60)
    except subprocess.TimeoutExpired:
        p.kill()
        rc = None
    check("setup pages clicked through (default button on each)", rc == 0 and len(pages) == 4, f"rc={rc} pages={pages}")
    check("setup window title", all(t.startswith("WackGet") and "Setup" in t for t in pages), str(pages))
    check("installed", os.path.exists(EXE) and os.path.exists(os.path.join(DIR, "uninst.exe")))
    check("listed in Apps & features", uninstall_entries() is not None)

    # 2. Install again over it (an upgrade)
    rc = subprocess.run([setup, "/S"]).returncode
    check("installing again over it works", rc == 0 and os.path.exists(EXE), f"rc={rc}")

    # 3. While WackGet runs: setup must not break it
    tmp = tempfile.mkdtemp()
    cfg = os.path.join(tmp, "w.ini")
    with open(cfg, "w") as f:
        f.write(f"DownloadDir={tmp}\\dl\nShowInSystray=0\n")
    wg = subprocess.Popen([EXE, f"--config={cfg}"])
    time.sleep(4)
    size_before = os.path.getsize(EXE)
    rc = subprocess.run([setup, "/S"], timeout=120).returncode
    check("setup while WackGet runs: stops (silent = Cancel)", rc != 0, f"rc={rc}")
    check("WackGet kept running and intact", wg.poll() is None and os.path.getsize(EXE) == size_before)
    wg.kill()
    wg.wait(10)
    time.sleep(1)
    rc = subprocess.run([setup, "/S"]).returncode
    check("after WackGet is closed, setup works again", rc == 0, f"rc={rc}")

    print(f"{len(failures)} failed: {failures}" if failures else "all passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
