# WackGet 2 (work in progress)

The classic WackGet download manager, kept looking and working the way it always did, but rebuilt
so it runs on today's computers: **Windows and Linux, 64-bit Intel/AMD and ARM**.

Status: **phase 1 of 3 — the core** (branch `modernize/p1-core`). The window comes in phase 2
(Windows) and phase 3 (Linux), built with wxWidgets so it uses the real Windows controls the
original used.

What phase 1 contains (`core/`):

- **Download engine:** libcurl on a thread per download, replacing the patched wget 1.10.2
  (2005). Same behaviour: keeps retrying, resumes partial files, skips files that are already up
  to date (wget `--timestamping`), sends the Referer for anti-leech links, proxy support. Modern
  TLS with certificate checks (Windows' own TLS and certificate store on Windows).
- **Paste and drag & drop:** finds links in pasted text and HTML, including the Windows
  CF_HTML clipboard format and relative links. No browser add-on: copy, paste, done.
- **.wgq queue files:** same format as 1.2.x (old queues still open, and 1.2.x can read new
  ones), now checked so a damaged or hostile queue file can't do harm.
- **The original texts:** the Status, Progress and Speed columns and the tray tooltip read exactly
  as before ("1,234K/5,120K; 24%", "2,048K/sec; 1:05").
- **Settings file** with the original setting names, instead of the registry.
- `wgdl`, a small command-line downloader for testing the core.

Security fixes over 1.2.4: no more command line
built from URLs (a .wgq file could inject wget options), no buffer overflows in the queue loader
or the HTML paste parser, safe local file names, no loading of WGET.EXE / cacert.pem from the
current folder.

## Building

Linux (Debian/Ubuntu):

    sudo apt install build-essential cmake libcurl4-openssl-dev python3
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ctest --test-dir build --output-on-failure

Windows (Visual Studio 2022 or Build Tools, x64 or ARM64; libcurl is built automatically):

    cmake -S . -B build -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure

Every push is built and tested on GitHub Actions: Windows x64 + ARM64, Linux x86_64 + aarch64,
Debian 13, AddressSanitizer/UBSan, and libFuzzer on the queue and paste parsers.

The 1.2.4 sources are still in `WackGet/` and `WGDLL/` for reference until the new window
replaces them.

---

## WackGet 1.2.4 (original README)

This is a modified version of Millweed's WackGet. 

See CHANGES.txt for the latest changes. 

-- OPENSSL CREDITS --
  "This product includes software developed by the OpenSSL Project
  for use in the OpenSSL Toolkit (http://www.openssl.org/)."

  "This product includes cryptographic software written by Eric Young 
  (eay@cryptsoft.com)"

  "This product includes software written by Tim Hudson 
  (tjh@cryptsoft.com)"
---------------------

It includes the latest version of the Windows binary for 
wget - v1.10.2 - source from http://users.ugent.be/~bpuype/wget/.

WackGet Official site: http://millweed.com/projects/wackget/

Mad props to the millweed.com guys for making such a nice little 
application and releasing the source!

Code by Paul Caristino and Andrew Armstrong at Mammoth Media. 

--
David Harrison
Mammoth Media
http://www.mammothmedia.com.au/




