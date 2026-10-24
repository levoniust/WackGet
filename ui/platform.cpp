/*
 * OS-specific bits: .wgq file association (original add_file_assoc(), now per user, no admin
 * rights), the taskbar button and the dark title bar for the skins.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#include <wx/frame.h>
#include <wx/stdpaths.h>

#include "frame.h"

#ifdef __WXMSW__
#include <windows.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>

static const wchar_t progid[]= L"WackGet.Queue";

static bool reg_set(const std::wstring &key, const wchar_t *name, const std::wstring &val)
{
  return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, val.c_str(),
                         (DWORD)((val.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;
}

static std::wstring reg_get(const std::wstring &key, const wchar_t *name)
{
  wchar_t buf[1024];
  DWORD sz= sizeof(buf);

  if(RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buf, &sz)!=ERROR_SUCCESS) {
    return std::wstring();
  }
  return buf;
}

bool platform_assoc_supported()
{
  return true;
}

bool platform_assoc_get()
{
  return reg_get(L"Software\\Classes\\.wgq", nullptr)==progid;
}

bool platform_assoc_set(bool on)
{
  std::wstring exe= wxStandardPaths::Get().GetExecutablePath().ToStdWstring();
  std::wstring cls= std::wstring(L"Software\\Classes\\")+progid;
  bool ok= true;

  if(on) {
    ok&= reg_set(L"Software\\Classes\\.wgq", nullptr, progid);
    ok&= reg_set(cls, nullptr, L"WackGet Queue");
    ok&= reg_set(cls+L"\\DefaultIcon", nullptr, L"\""+exe+L"\",0");
    ok&= reg_set(cls+L"\\shell\\open\\command", nullptr, L"\""+exe+L"\" \"%1\"");
  } else if(platform_assoc_get()) { /* only undo our own association */
    RegDeleteTreeW(HKEY_CURRENT_USER, cls.c_str());
    RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.wgq", nullptr);
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return ok;
}

void platform_set_taskbar_button(wxFrame *f, bool show)
{
  /* wxFRAME_NO_TASKBAR only works when the window is created; ITaskbarList changes it live. */
  ITaskbarList *tl= nullptr;

  if(SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList,
                                (void **)&tl))) {
    if(SUCCEEDED(tl->HrInit())) {
      HWND h= (HWND)f->GetHWND();
      if(show) {
        tl->AddTab(h);
      } else {
        tl->DeleteTab(h);
      }
    }
    tl->Release();
  }
}

void platform_dark_title_bar(wxFrame *f, bool dark)
{
  BOOL on= dark? TRUE: FALSE;

  /* DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1+ and 11); ignored by older versions */
  DwmSetWindowAttribute((HWND)f->GetHWND(), 20, &on, sizeof(on));
}

#else /* the association, tray and hot key for Linux are in linux_desktop.cpp */

void platform_set_taskbar_button(wxFrame *f, bool show)
{
  f->SetWindowStyleFlag(show? f->GetWindowStyleFlag()&~wxFRAME_NO_TASKBAR:
                              f->GetWindowStyleFlag()|wxFRAME_NO_TASKBAR);
}

void platform_dark_title_bar(wxFrame *, bool)
{
  /* the desktop draws the title bar */
}

#endif
