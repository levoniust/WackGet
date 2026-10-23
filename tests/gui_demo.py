#!/usr/bin/env python3
"""Starts WackGet against a local test server with a realistic queue and takes a screenshot of
each skin: downloads in progress, complete, failed (404), queued and paused items.
Usage: gui_demo.py PATH_TO_WACKGET OUTDIR [skin ...]
Linux: run under Xvfb (xvfb-run); needs ImageMagick's `import`. Windows: uses PowerShell."""
import os
import struct
import subprocess
import sys
import tempfile
import threading
import time
import http.server
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_engine  # noqa: E402  (the engine tests' server)

X, Y, W, H = 40, 40, 780, 300
SKINS = ["classic", "win11", "material1", "win11:dark", "material1:dark"]


class DemoHandler(run_engine.Handler):
    """/dl/<name>?size=N&rate=BYTES_PER_SEC serves N bytes at that rate; the rest as in run_engine."""

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        if not url.path.startswith("/dl/"):
            return super().do_GET()
        q = urllib.parse.parse_qs(url.query)
        size = int(q.get("size", ["100000"])[0])
        rate = int(q.get("rate", ["0"])[0])
        start = 0
        rng = self.headers.get("Range")
        if rng and rng.startswith("bytes="):
            start = int(rng[6:].split("-")[0])
        self.send_response(206 if start else 200)
        self.send_header("Content-Length", str(size - start))
        self.send_header("Accept-Ranges", "bytes")
        if start:
            self.send_header("Content-Range", f"bytes {start}-{size - 1}/{size}")
        self.end_headers()
        chunk = bytes(65536)
        sent = start
        t0 = time.time()
        try:
            while sent < size:
                n = min(len(chunk), size - sent)
                self.wfile.write(chunk[:n])
                sent += n
                if rate:
                    ahead = (sent - start) / rate - (time.time() - t0)
                    if ahead > 0:
                        time.sleep(ahead)
        except (BrokenPipeError, ConnectionResetError):
            pass


def write_queue(path, items):
    with open(path, "wb") as f:
        f.write(struct.pack("<i", 12))
        for url, status in items:
            b = url.encode()
            f.write(struct.pack("<i", len(b)) + b + struct.pack("<ii", status, 0))


def screenshot(out):
    if os.name == "nt":
        ps = ("Add-Type -AssemblyName System.Drawing;"
              f"$bmp=New-Object System.Drawing.Bitmap {W + 40},{H + 60};"
              "$g=[System.Drawing.Graphics]::FromImage($bmp);"
              f"$g.CopyFromScreen({X - 20},{Y - 20},0,0,$bmp.Size);"
              f"$bmp.Save('{out}')")
        subprocess.check_call(["powershell", "-NoProfile", "-Command", ps])
    else:
        subprocess.check_call(["import", "-window", "root", "-crop", f"{W}x{H}+{X}+{Y}", out])


def main():
    exe, outdir = sys.argv[1], sys.argv[2]
    skins = sys.argv[3:] or SKINS
    os.makedirs(outdir, exist_ok=True)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), DemoHandler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    queue = [
        (f"{base}/dl/debian-13.1.0-amd64-netinst.iso?size=790000000&rate=1800000", 0),
        (f"{base}/dl/WackGet-1.2.4-setup.exe?size=1450000", 0),
        (f"{base}/dl/Big_Buck_Bunny_1080p.mov?size=725000000&rate=950000", 0),
        (f"{base}/missing/old-mirror-file.zip", 0),
        (f"{base}/dl/blender-4.5.3-linux-x64.tar.xz?size=380000000&rate=2600000", 0),
        (f"{base}/dl/podcast-episode-212.mp3?size=91000000&rate=500000", 0),
        (f"{base}/dl/linux-6.17.tar.xz?size=150000000&rate=500000", 2),  # paused
    ]
    failures = 0
    for skin in skins:
        with tempfile.TemporaryDirectory(prefix="wg-gui-") as tmp:
            name, _, mode = skin.partition(":")
            cfg = os.path.join(tmp, "WackGet.ini")
            dl = os.path.join(tmp, "Downloads")
            with open(cfg, "w") as f:
                f.write(f"DownloadDir={dl}\nMaxThreads=4\nShowInSystray=0\n"
                        f"LastFrameX={X}\nLastFrameY={Y}\nLastFrameWidth={W}\nLastFrameHeight={H}\n"
                        "LastColumn0Width=290\nLastColumn1Width=110\nLastColumn2Width=200\nLastColumn3Width=150\n"
                        f"SkinDarkMode={2 if mode == 'dark' else 1}\n")
            wgq = os.path.join(tmp, "demo.wgq")
            write_queue(wgq, queue)
            p = subprocess.Popen([exe, f"--config={cfg}", f"--skin={name}", wgq])
            time.sleep(7)
            if p.poll() is not None:
                print(f"{skin}: WackGet exited early with {p.returncode}")
                failures += 1
                continue
            out = os.path.abspath(os.path.join(outdir, f"wackget-{skin.replace(':', '-')}.png"))
            screenshot(out)
            done = os.path.exists(os.path.join(dl, "WackGet-1.2.4-setup.exe"))
            print(f"{skin}: screenshot {out}; small file downloaded: {done}")
            failures += not done
            p.terminate()
            try:
                p.wait(10)
            except subprocess.TimeoutExpired:
                p.kill()
    server.shutdown()
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
