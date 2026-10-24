#!/usr/bin/env python3
"""End-to-end edge cases for the WackGet window, the way a person uses it, on Windows and Linux:
Unicode folders and file names, two links with the same file name, resuming after WackGet was
killed mid-download, a pasted web page (HTML clipboard), 300 pasted links at once, a second start
handing its link over, close-to-tray (Windows), the exit prompt, and (with --internet) real HTTPS
downloads including a redirect.

Usage: edge_test.py PATH_TO_WACKGET [--internet]
Linux: run under Xvfb with a window manager-capable session, e.g.
    xvfb-run -a -s "-screen 0 1280x800x24" python3 tests/edge_test.py build/wackget
  (needs openbox, xdotool, xclip). Windows: plain `python tests\\edge_test.py build\\Release\\WackGet.exe`.
Exits non-zero if a check fails."""
import hashlib
import http.server
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gui_demo import write_queue  # noqa: E402

WIN = os.name == "nt"
failures = []
ranges_seen = []


def check(what, ok, detail=""):
    print(("PASS " if ok else "FAIL ") + what + (f"  ({detail})" if detail and not ok else ""), flush=True)
    if not ok:
        failures.append(what)


def pattern(start, n):
    """Position-dependent bytes, so a resume at the wrong offset shows"""
    return bytes(((i * 7) ^ (i >> 8)) & 0xFF for i in range(start, start + n))


class Handler(http.server.BaseHTTPRequestHandler):
    """/data/<size>/<bytes per second, 0 = flat out>/<name>: pattern bytes, ranges supported.
    Anything after '?' is ignored (but becomes part of the file name, as in 1.2.x)."""
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def handle(self):
        try:
            super().handle()
        except ConnectionError:
            pass  # WackGet was killed or stopped mid-transfer: that's the test

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        _, _, size, rate, _ = url.path.split("/", 4)
        size, rate = int(size), int(rate)
        start = 0
        rng = self.headers.get("Range")
        if rng and rng.startswith("bytes="):
            start = int(rng[6:].split("-")[0])
            ranges_seen.append((url.path, start))
        if start >= size and size:
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{size}")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(206 if start else 200)
        self.send_header("Content-Length", str(size - start))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Last-Modified", "Wed, 01 Jan 2020 00:00:00 GMT")
        if start:
            self.send_header("Content-Range", f"bytes {start}-{size - 1}/{size}")
        self.end_headers()
        pos, t0 = start, time.time()
        try:
            while pos < size:
                n = min(65536, size - pos)
                self.wfile.write(pattern(pos, n))
                pos += n
                if rate:
                    ahead = (pos - start) / rate - (time.time() - t0)
                    if ahead > 0:
                        time.sleep(ahead)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


def good_file(path, size):
    if not os.path.exists(path) or os.path.getsize(path) != size:
        return False
    with open(path, "rb") as f:
        return f.read() == pattern(0, size)


def good_file_prefix(path):
    """What has arrived so far is the start of the pattern (nobody else wrote into it)"""
    with open(path, "rb") as f:
        data = f.read()
    return data == pattern(0, len(data))


def wait_for(cond, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.3)
    return cond()


# ---------------------------------------------------------------- windows and keys, per OS

if WIN:
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.SetForegroundWindow.argtypes = [wintypes.HWND]

    def windows(pid, title):
        found = []

        def cb(hwnd, _):
            p = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
            buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, buf, 256)
            if p.value == pid and buf.value == title and user32.IsWindowVisible(hwnd):
                found.append(hwnd)
            return True
        user32.EnumWindows(WNDENUMPROC(cb), 0)
        return found

    def close(pid):
        for h in windows(pid, "WackGet"):
            user32.PostMessageW(h, 0x0010, 0, 0)  # WM_CLOSE

    kernel32 = ctypes.WinDLL("kernel32")
    user32.GetForegroundWindow.restype = wintypes.HWND

    def focus(hwnd):
        """Windows only lets the program the user last used change the foreground window, so
        borrow the foreground thread's input state for a moment. True if it worked."""
        for _ in range(5):
            fg = user32.GetForegroundWindow()
            me = kernel32.GetCurrentThreadId()
            other = user32.GetWindowThreadProcessId(fg, None) if fg else 0
            if other and other != me:
                user32.AttachThreadInput(me, other, True)
            user32.keybd_event(0x12, 0, 0, 0)  # an Alt tap also unlocks it
            user32.keybd_event(0x12, 0, 2, 0)
            user32.ShowWindow(hwnd, 5)  # SW_SHOW
            user32.BringWindowToTop(hwnd)
            user32.SetForegroundWindow(hwnd)
            if other and other != me:
                user32.AttachThreadInput(me, other, False)
            time.sleep(0.4)
            if user32.GetForegroundWindow() == hwnd:
                return True
        return False

    def command(pid, title, cmd):
        """Send a menu/button command straight to the window (no focus needed)"""
        for h in windows(pid, title):
            user32.PostMessageW(h, 0x0111, cmd, 0)  # WM_COMMAND

    def keys(pid, title, combo):
        """Bring the window forward and type a key combination like 'ctrl+v' or 'n'. Returns
        False if the window couldn't be made the foreground one (then the keys are not sent)."""
        hs = windows(pid, title)
        if not hs or not focus(hs[0]):
            print(f"  (could not bring '{title}' to the front for {combo})", flush=True)
            return False
        vk = {"ctrl": 0x11, "alt": 0x12}
        parts = combo.split("+")
        mods = [vk[p] for p in parts[:-1]]
        key = ord(parts[-1].upper())
        for m in mods:
            user32.keybd_event(m, 0, 0, 0)
        user32.keybd_event(key, 0, 0, 0)
        user32.keybd_event(key, 0, 2, 0)
        for m in reversed(mods):
            user32.keybd_event(m, 0, 2, 0)
        return True

    def set_clipboard(text, html=False):
        """PowerShell 5's Set-Clipboard -AsHtml makes the CF_HTML a browser would"""
        fd, path = tempfile.mkstemp(suffix=".txt")
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(text)
        flag = "-AsHtml " if html else ""
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        f"Set-Clipboard {flag}-Value (Get-Content -Raw -Encoding UTF8 '{path}')"], check=True)
        os.remove(path)

    def kill(p):
        p.kill()  # TerminateProcess: like a crash or a power cut
else:
    def _xdo(*args):
        return subprocess.run(["xdotool", *args], capture_output=True, text=True, timeout=20)

    def windows(pid, title):
        r = _xdo("search", "--onlyvisible", "--pid", str(pid), "--name", f"^{title}$")
        return r.stdout.split()

    def close(pid):
        for w in windows(pid, "WackGet"):
            _xdo("windowactivate", "--sync", w)
            _xdo("key", "alt+F4")

    def keys(pid, title, combo):
        ws = windows(pid, title)
        if not ws:
            return False
        _xdo("windowactivate", "--sync", ws[0])
        time.sleep(0.3)
        _xdo("key", combo)
        return True

    def set_clipboard(text, html=False):
        p = subprocess.Popen(["xclip", "-selection", "clipboard", "-t", "text/html" if html else "UTF8_STRING"],
                             stdin=subprocess.PIPE)
        p.communicate(text.encode())

    def kill(p):
        p.kill()

    def command(pid, title, cmd):
        pass


# ---------------------------------------------------------------- the tests

class Case:
    """A fresh settings file and download folder per case"""

    def __init__(self, name, extra="", dirname="Downloads"):
        self.tmp = tempfile.mkdtemp(prefix=f"wg-edge-{name}-")
        self.dl = os.path.join(self.tmp, dirname)
        self.cfg = os.path.join(self.tmp, "wackget.ini")
        with open(self.cfg, "w", encoding="utf-8") as f:
            f.write(f"DownloadDir={self.dl}\nShowInSystray=0\nMaxThreads=4\n{extra}")
        self.procs = []

    def start(self, *args):
        p = subprocess.Popen([EXE, f"--config={self.cfg}", *args])
        self.procs.append(p)
        return p

    def path(self, name):
        return os.path.join(self.dl, name)

    def done(self):
        for p in self.procs:
            if p.poll() is None:
                p.kill()
                p.wait(10)
        time.sleep(0.5)
        shutil.rmtree(self.tmp, ignore_errors=True)


def t_unicode():
    c = Case("unicode", dirname="Téléchargements 日本 ñ")
    try:
        p = c.start(f"{BASE}/data/5000/0/caf%C3%A9%20%E6%97%A5%E6%9C%AC.bin")
        check("Unicode folder + %-encoded Unicode file name",
              wait_for(lambda: good_file(c.path("café 日本.bin"), 5000), 20),
              str(os.listdir(c.dl) if os.path.isdir(c.dl) else "no folder"))
        check("still running", p.poll() is None)
    finally:
        c.done()


def t_same_name():
    """Links that differ only in the query get their own files, as in 1.2.x ("d.php@id=1"). Two
    links with really the same file name take turns: never both writing the file at once."""
    c = Case("samename")
    try:
        c.start(f"{BASE}/data/3000/0/d.php?id=1", f"{BASE}/data/4000/0/d.php?id=2")
        check("query in the name: d.php@id=1 and d.php@id=2",
              wait_for(lambda: good_file(c.path("d.php@id=1"), 3000) and good_file(c.path("d.php@id=2"), 4000), 20),
              str(os.listdir(c.dl) if os.path.isdir(c.dl) else ""))
    finally:
        c.done()
    c = Case("samename2")
    try:
        c.start(f"{BASE}/data/3000000/1000000/same.bin", f"{BASE}/other/4000/0/same.bin")
        time.sleep(1.5)
        check("same file name: the second waits while the first writes",
              os.path.isdir(c.dl) and os.listdir(c.dl) == ["same.bin"] and good_file_prefix(c.path("same.bin")))
        check("and the file ends up whole, not a mix",
              wait_for(lambda: good_file(c.path("same.bin"), 3000000) or good_file(c.path("same.bin"), 4000), 30))
    finally:
        c.done()


def t_long_link():
    """A signed download link with a long query: on Windows the name is shortened so the whole
    path stays under 260 characters (Explorer can open it)"""
    c = Case("longlink")
    sig = "a1b2c3d4" * 40
    try:
        c.start(f"{BASE}/data/2000/0/report-final.pdf?X-Amz-Expires=600&X-Amz-Signature={sig}")
        ok = wait_for(lambda: os.path.isdir(c.dl) and len(os.listdir(c.dl)) == 1 and
                      good_file(c.path(os.listdir(c.dl)[0]), 2000), 20)
        names = os.listdir(c.dl) if os.path.isdir(c.dl) else []
        check("long signed link downloaded", ok and names[0].startswith("report-final.pdf@X-Amz-Expires=600"), str(names))
        if WIN and names:
            full = len(c.path(names[0]))
            check("Windows: whole path under 260 characters", full <= 259, f"{full}")
    finally:
        c.done()


def t_crash_resume():
    c = Case("resume")
    size = 30_000_000
    try:
        wgq = os.path.join(c.tmp, "big.wgq")
        write_queue(wgq, [(f"{BASE}/data/{size}/3000000/big.bin", 0)])
        p = c.start(wgq)
        part = lambda: max((os.path.getsize(os.path.join(c.dl, n)) for n in os.listdir(c.dl)), default=0) \
            if os.path.isdir(c.dl) else 0
        wait_for(lambda: part() >= 4_000_000, 30)
        got = part()
        kill(p)
        p.wait(10)
        check("killed mid-download with a partial file", 0 < got < size, f"{got}")
        ranges_seen.clear()
        c.start(wgq)
        check("restarted from the queue: finishes, bytes identical",
              wait_for(lambda: good_file(c.path("big.bin"), size), 60),
              str({n: os.path.getsize(c.path(n)) for n in os.listdir(c.dl)}))
        check("it resumed (asked for the rest only)", any(s > 0 for _, s in ranges_seen), str(ranges_seen))
    finally:
        c.done()


def t_paste_html():
    c = Case("pastehtml")
    try:
        p = c.start()
        wait_for(lambda: windows(p.pid, "WackGet"), 15)
        set_clipboard(f'<p>Files: <a href="{BASE}/data/2500/0/from%20page.bin">from page</a> and '
                      f'<a href="javascript:alert(1)">bad</a></p>', html=True)
        sent = keys(p.pid, "WackGet", "ctrl+v")
        got = wait_for(lambda: good_file(c.path("from page.bin"), 2500), 20 if sent else 2)
        if sent or not WIN:
            check("Ctrl+V of a web page (HTML clipboard): link downloaded", got,
                  str(os.listdir(c.dl) if os.path.isdir(c.dl) else "nothing"))
        if WIN and not got:  # no keyboard focus on this desktop: use the menu command instead
            command(p.pid, "WackGet", 5033)  # wxID_PASTE = Queue > Add From Clipboard
            got = wait_for(lambda: good_file(c.path("from page.bin"), 2500), 20)
            check("Add From Clipboard of a web page (HTML clipboard): link downloaded", got,
                  str(os.listdir(c.dl) if os.path.isdir(c.dl) else "nothing"))
        check("javascript: link ignored", os.path.isdir(c.dl) and len(os.listdir(c.dl)) == 1)
    finally:
        c.done()


def t_paste_many():
    c = Case("pastemany")
    n = 300
    try:
        p = c.start()
        wait_for(lambda: windows(p.pid, "WackGet"), 15)
        set_clipboard("\n".join(f"{BASE}/data/{1000 + i}/0/f{i:03d}.bin" for i in range(n)))
        if not keys(p.pid, "WackGet", "ctrl+v"):
            command(p.pid, "WackGet", 5033)
        ok = wait_for(lambda: os.path.isdir(c.dl) and len(os.listdir(c.dl)) >= n, 120)
        bad = [i for i in range(n) if not good_file(c.path(f"f{i:03d}.bin"), 1000 + i)]
        check(f"{n} pasted links all downloaded intact", ok and not bad, f"bad: {bad[:10]}")
        check("still running", p.poll() is None)
    finally:
        c.done()


def t_second_instance():
    c = Case("second")
    try:
        a = c.start()
        wait_for(lambda: windows(a.pid, "WackGet"), 15)
        b = c.start(f"{BASE}/data/1500/0/handed.bin")
        try:
            rc = b.wait(20)
        except subprocess.TimeoutExpired:
            rc = None
        check("second start hands its link over and quits", rc == 0, f"rc={rc}")
        check("the first one downloads it", wait_for(lambda: good_file(c.path("handed.bin"), 1500), 20))
    finally:
        c.done()


def t_exit_prompt():
    c = Case("exitprompt")
    try:
        wgq = os.path.join(c.tmp, "q.wgq")
        write_queue(wgq, [(f"{BASE}/data/1000/0/paused.bin", 2)])  # paused: unfinished
        p = c.start(wgq)
        wait_for(lambda: windows(p.pid, "WackGet"), 15)
        time.sleep(1)
        close(p.pid)
        asked = wait_for(lambda: windows(p.pid, "Confirm"), 10)
        check("closing with an unfinished queue asks to save it", asked)
        if asked:
            if WIN:
                command(p.pid, "Confirm", 7)  # IDNO: the message box's No button
            else:
                keys(p.pid, "Confirm", "alt+n")
        try:
            p.wait(10)
            check("No: WackGet exits", True)
        except subprocess.TimeoutExpired:
            check("No: WackGet exits", False)
    finally:
        c.done()


def t_tray_close():
    c = Case("tray", extra="ShowInSystray=1\n")
    try:
        a = c.start()
        wait_for(lambda: windows(a.pid, "WackGet"), 15)
        time.sleep(1)
        close(a.pid)
        time.sleep(2)
        check("with the tray icon, closing hides the window", a.poll() is None and not windows(a.pid, "WackGet"))
        b = c.start()
        try:
            b.wait(20)
        except subprocess.TimeoutExpired:
            pass
        check("starting it again brings the window back", wait_for(lambda: windows(a.pid, "WackGet"), 10))
    finally:
        c.done()


def t_internet():
    c = Case("internet")
    urls = {
        "curl-8.23.0.tar.xz.asc": "https://curl.se/download/curl-8.23.0.tar.xz.asc",
        # a GitHub release asset: redirects to another host
        "curl-8.17.0.tar.xz.asc": "https://github.com/curl/curl/releases/download/curl-8_17_0/curl-8.17.0.tar.xz.asc",
    }
    try:
        c.start(*urls.values())
        for name in urls:
            path = c.path(name)
            ok = wait_for(lambda: os.path.exists(path) and os.path.getsize(path) > 100, 60)
            head = open(path, "rb").read(40) if os.path.exists(path) else b""
            check(f"real HTTPS download: {name}", ok and head.startswith(b"-----BEGIN PGP SIGNATURE"), repr(head))
    finally:
        c.done()


def main():
    global EXE, BASE
    EXE = os.path.abspath(sys.argv[1])
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    BASE = f"http://127.0.0.1:{server.server_address[1]}"
    wm = None
    if not WIN:
        wm = subprocess.Popen(["openbox"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1)
    tests = [t_unicode, t_same_name, t_long_link, t_crash_resume, t_paste_html, t_paste_many, t_second_instance,
             t_exit_prompt]
    if WIN:
        tests.append(t_tray_close)  # Linux tray: tests/linux_desktop.py
    if "--internet" in sys.argv:
        tests.append(t_internet)
    for t in tests:
        print(f"-- {t.__name__[2:]}", flush=True)
        try:
            t()
        except Exception as e:  # a crash in one case should not hide the others
            check(f"{t.__name__} ran", False, repr(e))
    if wm:
        wm.terminate()
    server.shutdown()
    print(f"{len(failures)} failed: {failures}" if failures else "all passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
