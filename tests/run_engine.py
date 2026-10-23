#!/usr/bin/env python3
"""Runs tests/engine.c against a local HTTP server that imitates the awkward servers WackGet meets:
ranges or no ranges, redirects (including one to file://), errors, flaky and dropped connections,
slow transfers, chunked replies. Usage: run_engine.py PATH_TO_wg_engine"""
import email.utils
import http.server
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

SIZE = 200_000
SLOW_SIZE = 2_000_000
MTIME = 1577836800  # 2020-01-01, sent as Last-Modified
DATA = bytes(i % 251 for i in range(max(SIZE, SLOW_SIZE)))
hits = {}
lock = threading.Lock()


def count(key):
    with lock:
        hits[key] = hits.get(key, 0) + 1
        return hits[key]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send_body(self, body, ranges=True, status=200, chunk=None, delay=0.0, drop_after=None):
        start = 0
        rng = self.headers.get("Range")
        if ranges and rng and rng.startswith("bytes="):
            start = int(rng[6:].split("-")[0])
            if start >= len(body):
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{len(body)}")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            status = 206
        part = body[start:]
        self.send_response(status)
        self.send_header("Content-Length", str(len(part)))
        self.send_header("Last-Modified", email.utils.formatdate(MTIME, usegmt=True))
        if ranges:
            self.send_header("Accept-Ranges", "bytes")
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{len(body) - 1}/{len(body)}")
        self.end_headers()
        if self.command == "HEAD":
            return
        step = chunk or len(part) or 1
        sent = 0
        for i in range(0, len(part), step):
            if drop_after is not None and sent >= drop_after:
                self.close_connection = True
                self.wfile.flush()
                self.connection.shutdown(2)
                return
            self.wfile.write(part[i:i + step])
            sent += len(part[i:i + step])
            if delay:
                self.wfile.flush()
                time.sleep(delay)

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(url.query)
        key = url.path + "?" + q.get("id", [""])[0]
        p = url.path
        if p == "/file.bin":
            self.send_body(DATA[:SIZE])
        elif p == "/slow.bin":
            self.send_body(DATA[:SLOW_SIZE], chunk=32768, delay=0.02)
        elif p == "/norange.bin":
            self.send_body(DATA[:SIZE], ranges=False)
        elif p == "/redirect":
            self.redirect("/file.bin")
        elif p == "/redirect-file":
            self.redirect("file:///etc/passwd")
        elif p == "/missing":
            self.plain(404, b"<html>not found</html>")
        elif p == "/flaky.bin":
            if count(key) == 1:
                self.plain(503, b"busy")
            else:
                self.send_body(DATA[:SIZE])
        elif p == "/always503":
            self.plain(503, b"busy")
        elif p == "/drop.bin":
            if count(key) == 1:
                self.send_body(DATA[:SIZE], chunk=10000, drop_after=SIZE // 2)
            else:
                self.send_body(DATA[:SIZE])
        elif p == "/referer":
            self.plain(200, (self.headers.get("Referer") or "none").encode())
        elif p == "/chunked.bin":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, SIZE, 50000):
                part = DATA[i:i + 50000]
                self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        elif p == "/empty":
            self.plain(200, b"")
        else:
            self.plain(404, b"no such test")

    def redirect(self, where):
        self.send_response(302)
        self.send_header("Location", where)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def plain(self, status, body):
        self.send_response(status)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)


def main():
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    with tempfile.TemporaryDirectory(prefix="wg-engine-") as tmp:
        rc = subprocess.call([sys.argv[1], str(port), tmp])
    server.shutdown()
    sys.exit(rc)


if __name__ == "__main__":
    main()
