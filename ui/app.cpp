/*
 * Start-up: one WackGet per user (a second start hands its URLs and .wgq files to the first, like
 * the original send_command_line()), settings, and the main window.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#include <cstdlib>
#include <functional>
#include <memory>

#include <wx/app.h>
#include <wx/filename.h>
#include <wx/ipc.h>
#include <wx/msgdlg.h>
#include <wx/snglinst.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

#ifndef __WXMSW__
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "frame.h"
#include "prefs.h"

namespace {

const char ipc_topic[]= "WackGet";
const size_t ipc_max= 4*1024*1024; /* a long list of pasted links, but nothing silly */

MainFrame *main_frame;

/* Arguments arrive as "cmd\nARG\nARG...": cmd is "open" (add/load) or "show". */
class Conn : public wxConnection {
public:
  bool OnExec(const wxString &, const wxString &data) override
  {
    if(!main_frame || data.length()>ipc_max) {
      return false;
    }
    wxArrayString parts= wxSplit(data, '\n', '\0');
    std::vector<std::string> args;

    for(size_t i= 1; i<parts.size() && args.size()<100000; i++) {
      args.push_back(parts[i].utf8_string());
    }
    main_frame->CallAfter([args] {
      if(args.empty()) {
        main_frame->show_from_outside();
      } else {
        main_frame->process_args(args, false); /* asks before replacing a non-empty queue */
      }
    });
    return true;
  }
};

class Server : public wxServer {
public:
  wxConnectionBase *OnAcceptConnection(const wxString &topic) override
  {
    return topic==ipc_topic? new Conn: nullptr;
  }
};

class App : public wxApp {
public:
  bool OnInit() override;
  int OnExit() override;

private:
  wxString service_name(const wxString &instance);
  std::unique_ptr<wxSingleInstanceChecker> checker_;
  std::unique_ptr<Server> server_;
  wxString socket_path_;
};

wxString App::service_name(const wxString &instance)
{
#ifdef __WXMSW__
  return "WackGet-"+instance; /* DDE service, per user session */
#else
  /* Unix socket in the per-user runtime folder (mode 0700), else next to the settings */
  wxString dir;
  if(!wxGetEnv("XDG_RUNTIME_DIR", &dir) || dir.empty()) {
    dir= wxString::FromUTF8(prefs::dir());
  }
  socket_path_= dir+"/"+instance+".sock";
  return socket_path_;
#endif
}

bool App::OnInit()
{
  std::vector<std::string> args;
  wxString config, skin_override;

  for(int i= 1; i<argc; i++) {
    wxString a= argv[i];

    if(a.StartsWith("--config=", &config)) {
      continue;
    }
    if(a.StartsWith("--skin=", &skin_override)) {
      continue;
    }
    args.push_back(a.utf8_string());
  }
  if(!config.empty()) {
    config= wxFileName(config).GetAbsolutePath();
  }
  prefs::init(config.empty()? nullptr: (const char *)config.utf8_str());
  if(!skin_override.empty()) { /* for screenshots and testing: not saved unless Preferences is used */
    prefs::set_str("Skin", skin_name(skin_from_name(skin_override)));
  }

  /* One instance per user (and per settings file, so tests can run their own). */
  wxString instance= "wackget-"+wxGetUserId();
  if(!config.empty()) {
    instance+= wxString::Format("-%08lx", (unsigned long)std::hash<std::string>()(config.utf8_string()));
  }
  checker_.reset(new wxSingleInstanceChecker(instance+".lock", wxString::FromUTF8(prefs::dir())));
  bool primary= !checker_->IsAnotherRunning();
  if(!primary) {
    wxClient client;
    std::unique_ptr<wxConnectionBase> conn(client.MakeConnection("localhost", service_name(instance), ipc_topic));

    if(conn) {
      wxString msg= args.empty()? "show": "open";
      for(const auto &a : args) {
        msg+= "\n"+wxString::FromUTF8(a);
      }
      conn->Execute(msg);
      conn->Disconnect();
      prefs::cleanup();
      std::exit(0); /* handed over: success, like the original's return 0 */
    }
    /* The other one isn't answering (still starting, or a stale lock): carry on. */
  }

#if wxCHECK_VERSION(3, 3, 0) && defined(__WXMSW__)
  /* Dark menus and dialogs for the skins (needs to happen before any window exists) */
  if(skin_from_name(prefs::get_str("Skin", "classic"))!=SkinId::Classic) {
    int dm= prefs::get_int("SkinDarkMode", 0);
    if(dm==0) {
      MSWEnableDarkMode(DarkMode_Auto);
    } else if(dm==2) {
      MSWEnableDarkMode(DarkMode_Always);
    }
  }
#endif

  if(wg_global_init()!=WG_OK) {
    wxMessageBox("Could not start the download engine (libcurl).", "WackGet", wxOK|wxICON_ERROR);
    return false;
  }

  server_.reset(new Server);
  wxString svc= service_name(instance);
#ifndef __WXMSW__
  if(primary) {
    unlink(socket_path_.fn_str()); /* left over from a crash; the lock says we're the only one */
  }
#endif
  if(server_->Create(svc)) {
#ifndef __WXMSW__
    chmod(socket_path_.fn_str(), 0600);
#endif
  } else {
    server_.reset();
  }

  main_frame= new MainFrame;
  SetTopWindow(main_frame);
  main_frame->show_initially();
  main_frame->process_args(args, true); /* Add command line parms to list */
  return true;
}

int App::OnExit()
{
  main_frame= nullptr;
#ifndef __WXMSW__
  if(server_ && !socket_path_.empty()) {
    unlink(socket_path_.fn_str());
  }
#endif
  server_.reset();
  wg_global_cleanup();
  prefs::cleanup();
  return wxApp::OnExit();
}

} // namespace

wxIMPLEMENT_APP(App);
