; WackGet setup: the 1.2.x installer's pages and choices (licence, components, folder), now per
; user (no administrator rights, like the settings and the .wgq association) and with one setup
; for both x64 and ARM64 PCs.
;
; makensis -DVERSION=2.0.0 -DX64=dir/with/x64/WackGet.exe -DARM64=dir/with/arm64/WackGet.exe
;          -DOUTFILE=WackGet-2.0.0-setup.exe windows/installer.nsi
; (NSIS 3.08 or newer; works from Linux too.)

!ifndef VERSION | X64 | ARM64 | OUTFILE
  !error "define VERSION, X64, ARM64 and OUTFILE"
!endif
!define ROOT ".."
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\WackGet"
!define PROGID "WackGet.Queue"

!include LogicLib.nsh
!include WinVer.nsh
!include x64.nsh

Unicode true
ManifestDPIAware true
ManifestSupportedOS Win10
RequestExecutionLevel user
SetCompressor /SOLID lzma
XPStyle on

Name "WackGet"
Caption "WackGet ${VERSION} Setup"
OutFile "${OUTFILE}"
Icon "${ROOT}\WackGet\res\wackget.ico"
UninstallIcon "${ROOT}\WackGet\res\wackget.ico"
BrandingText "WackGet ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "WackGet"
VIAddVersionKey "FileDescription" "WackGet Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2002-2003 Millweed; GNU GPL v2"

LicenseText "WackGet is free software released under the terms of the GNU General Public License."
LicenseData "${ROOT}\LICENSE.txt"

InstallDir "$LOCALAPPDATA\Programs\WackGet"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
DirText "Setup has determined the default location to install.  To change the location, click Browse below or enter a path manually." "Select the installation folder:"
ComponentText "This will install WackGet ${VERSION}" "" "Install with the following components and options:"

Page license
Page components
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Function .onInit
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP "WackGet ${VERSION} needs Windows 10 or 11."
    Abort
  ${EndIf}
  ${IfNot} ${IsNativeAMD64}
  ${AndIfNot} ${IsNativeARM64}
    MessageBox MB_OK|MB_ICONSTOP "WackGet ${VERSION} needs 64-bit Windows (x64 or ARM64)."
    Abort
  ${EndIf}
FunctionEnd

; WackGet.exe can't be replaced while it runs: ask, like 1.2.x did for WGDLL.dll
!macro CLOSE_WACKGET LABEL
  ${LABEL}:
  ClearErrors
  ${If} ${FileExists} "$INSTDIR\WackGet.exe"
    FileOpen $0 "$INSTDIR\WackGet.exe" a
    ${If} ${Errors}
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "WackGet is running.  Please exit WackGet (File > Exit, or Exit in its tray menu) and press Retry." /SD IDCANCEL IDRETRY ${LABEL}
      Abort
    ${EndIf}
    FileClose $0
  ${EndIf}
!macroend

Section "WackGet (required)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  !insertmacro CLOSE_WACKGET retry_install
  ${If} ${IsNativeARM64}
    File "${ARM64}\WackGet.exe"
  ${Else}
    File "${X64}\WackGet.exe"
  ${EndIf}
  File "${ROOT}\LICENSE.txt"
  File "${ROOT}\WackGet.html"
  WriteUninstaller "$INSTDIR\uninst.exe"

  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "WackGet"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "WackGet contributors"
  WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "https://github.com/levoniust/WackGet"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" '"$INSTDIR\WackGet.exe",0'
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninst.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninst.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" 8192
SectionEnd

Section "Start Menu shortcuts" SecStartMenu
  CreateShortCut "$SMPROGRAMS\WackGet.lnk" "$INSTDIR\WackGet.exe"
SectionEnd

Section /o "Desktop shortcut" SecDesktop
  CreateShortCut "$DESKTOP\WackGet.lnk" "$INSTDIR\WackGet.exe"
SectionEnd

; The same keys as Preferences > Integration writes (ui/platform.cpp)
Section "Associate .wgq files with WackGet" SecAssoc
  WriteRegStr HKCU "Software\Classes\.wgq" "" "${PROGID}"
  WriteRegStr HKCU "Software\Classes\${PROGID}" "" "WackGet Queue"
  WriteRegStr HKCU "Software\Classes\${PROGID}\DefaultIcon" "" '"$INSTDIR\WackGet.exe",0'
  WriteRegStr HKCU "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\WackGet.exe" "%1"'
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)' ; SHCNE_ASSOCCHANGED
SectionEnd

Section "Uninstall"
  !insertmacro CLOSE_WACKGET retry_uninstall
  Delete "$INSTDIR\WackGet.exe"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\WackGet.html"
  Delete "$INSTDIR\uninst.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\WackGet.lnk"
  Delete "$DESKTOP\WackGet.lnk"

  ; Only undo the association if it is still ours
  ReadRegStr $0 HKCU "Software\Classes\.wgq" ""
  ${If} $0 == "${PROGID}"
    DeleteRegKey HKCU "Software\Classes\.wgq"
  ${EndIf}
  DeleteRegKey HKCU "Software\Classes\${PROGID}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

  DeleteRegKey HKCU "${UNINST_KEY}"

  ; 1.2.x removed its settings too; now it asks first (and never when run with /S)
  ${If} ${FileExists} "$APPDATA\WackGet\*.*"
    MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Also remove your WackGet settings?" /SD IDNO IDNO keep_settings
    RMDir /r "$APPDATA\WackGet"
    System::Call 'advapi32::CredDeleteW(w "WackGet/Proxy", i 1, i 0)' ; the proxy password (CRED_TYPE_GENERIC)
    keep_settings:
  ${EndIf}
SectionEnd
