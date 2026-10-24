# WackGet 2

The classic WackGet download manager, kept looking and working the way it always did, but rebuilt
so it runs on today's computers: **Windows and Linux, 64-bit Intel/AMD and ARM**.

**Download: [latest release](https://github.com/levoniust/WackGet/releases/latest)** (Windows
setup or zip, Linux .deb).

## Installing

From the [releases page](https://github.com/levoniust/WackGet/releases/latest) (every
[Actions run](https://github.com/levoniust/WackGet/actions) also has them, under Artifacts):

- **Windows 10/11:** `WackGet-2.0.0-setup.exe` — one setup for x64 and ARM64 PCs. It installs for your user
  only (no administrator prompt) into `%LOCALAPPDATA%\Programs\WackGet`, with a Start Menu
  shortcut and, if you leave it ticked, the .wgq association. Not code-signed yet, so SmartScreen
  asks first ("More info" > "Run anyway"). Or take `WackGet-2.0.0-windows-x64.zip` / `-ARM64.zip` and
  run `WackGet.exe` from anywhere.
- **Debian 13 / Ubuntu 24.04 and newer:** `wackget_2.0.0_amd64.deb` or `_arm64.deb`, then
  `sudo apt install ./wackget_2.0.0_amd64.deb`.

On Linux:

- **Tray icon:** shows on KDE Plasma, Xfce, Cinnamon, MATE and others. Stock GNOME has no tray:
  install the "AppIndicator and KStatusNotifierItem Support" extension
  (`gnome-shell-extension-appindicator` on Debian; Ubuntu has it already). Without a tray,
  closing the window quits WackGet instead of hiding it. The tray menu has Show/Hide, since
  panels open the menu on a left click.
- **Hot key** (Preferences > Integration): Ctrl+Alt+W on X11. On Wayland, WackGet asks the
  desktop through the shortcuts portal (GNOME 48+, Plasma 6), which asks you once and may let you
  pick another key. Anywhere else, make a keyboard shortcut in your desktop settings that runs
  `wackget --toggle`.
- **.wgq files:** the .deb sets up the file type; Preferences > Integration makes WackGet the app
  that opens them (and for a copy you built yourself, adds it to your app menu).

## Phase 1: the core

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

    sudo apt install build-essential cmake libcurl4-openssl-dev python3 libwxgtk3.2-dev libgtk-3-dev \
      libsecret-1-dev pkg-config
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ctest --test-dir build --output-on-failure
    cd build && cpack    # wackget_<version>_<arch>.deb

The tray icon uses `libayatana-appindicator3-1` when it is installed (no build dependency).

Windows (Visual Studio 2022 or Build Tools, x64 or ARM64; libcurl is built automatically):

    cmake -S . -B build -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure

The Windows setup is made with NSIS (`windows/installer.nsi`, also from Linux with `makensis`).

Every push is built and tested on GitHub Actions: Windows x64 + ARM64, Linux x86_64 + aarch64,
Debian 13, AddressSanitizer/UBSan, and libFuzzer on the queue and paste parsers. The window is
tested in every skin against a local download server, the Linux tray/hot key/association against
a stand-in panel, the .deb on a clean Debian 13, and the setup by installing and uninstalling it
on Windows x64 and ARM64.

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




