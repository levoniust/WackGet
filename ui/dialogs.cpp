/*
 * Add new item, About and Preferences, laid out like the original dialogs (wackget.rc).
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#include <wx/bookctrl.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/panel.h>
#include <wx/propdlg.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include "frame.h"
#include "prefs.h"

/* ------------------------------------------------------------------ Add new item (IDD_ADDITEM) */

bool run_add_item_dialog(wxWindow *parent, std::vector<std::string> &urls)
{
  wxDialog dlg(parent, wxID_ANY, "Add new item...");
  auto *sz= new wxBoxSizer(wxVERTICAL);
  auto *text= new wxTextCtrl(&dlg, wxID_ANY, wxString(), wxDefaultPosition, wxSize(dlg.FromDIP(380), -1));

  sz->Add(new wxStaticText(&dlg, wxID_ANY, "&Enter the URL of the new item:"), 0, wxLEFT|wxRIGHT|wxTOP, dlg.FromDIP(8));
  sz->Add(text, 0, wxEXPAND|wxALL, dlg.FromDIP(8));
  sz->Add(dlg.CreateStdDialogButtonSizer(wxOK|wxCANCEL), 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, dlg.FromDIP(8));
  dlg.SetSizerAndFit(sz);
  dlg.CentreOnParent();
  text->SetFocus();

  dlg.Bind(wxEVT_BUTTON, [&](wxCommandEvent &e) {
    std::string s= text->GetValue().Trim().Trim(false).utf8_string();

    /* Several links pasted at once: take them all. Otherwise prepend a scheme if none was typed. */
    urls= urls_from_text(s);
    if(urls.empty() && !s.empty()) {
      if(s.find(":/")==std::string::npos) {
        s= "https://"+s;
      }
      if(wg_url_is_supported(s.c_str())) {
        urls.push_back(s);
      }
    }
    if(urls.empty()) {
      wxBell();
      text->SetFocus();
      return;
    }
    e.Skip();
  }, wxID_OK);
  return dlg.ShowModal()==wxID_OK && !urls.empty();
}

/* ------------------------------------------------------------------ About (IDD_ABOUTBOX) */

void run_about_dialog(MainFrame *parent)
{
  wxDialog dlg(parent, wxID_ANY, "About WackGet");
  auto *top= new wxBoxSizer(wxHORIZONTAL);
  auto *col= new wxBoxSizer(wxVERTICAL);
  int gap= dlg.FromDIP(4);

  top->Add(new wxStaticBitmap(&dlg, wxID_ANY, wxBitmap(parent->icons().GetIcon(wxSize(48, 48)))), 0,
           wxALL, dlg.FromDIP(12));
  col->Add(new wxStaticText(&dlg, wxID_ANY, "WackGet version " WG_VERSION), 0, wxBOTTOM, gap);
  col->Add(new wxHyperlinkCtrl(&dlg, wxID_ANY, "https://github.com/levoniust/WackGet",
                               "https://github.com/levoniust/WackGet"), 0, wxBOTTOM, gap);
  col->Add(new wxStaticText(&dlg, wxID_ANY, "Copyright (C) 2002-2003 Millweed"), 0, wxBOTTOM, gap);
  col->Add(new wxStaticText(&dlg, wxID_ANY, "Based on WackGet 1.2.4-Mammoth"), 0, wxBOTTOM, gap);
  auto *lic= new wxBoxSizer(wxHORIZONTAL);
  lic->Add(new wxStaticText(&dlg, wxID_ANY, "License:"), 0, wxRIGHT|wxALIGN_CENTER_VERTICAL, gap);
  lic->Add(new wxHyperlinkCtrl(&dlg, wxID_ANY, "GNU General Public License",
                               "https://www.gnu.org/licenses/old-licenses/gpl-2.0.html"), 0, wxALIGN_CENTER_VERTICAL);
  col->Add(lic);
  top->Add(col, 1, wxTOP|wxBOTTOM, dlg.FromDIP(12));
  auto *ok= new wxButton(&dlg, wxID_OK, "OK");
  ok->SetDefault();
  top->Add(ok, 0, wxALL, dlg.FromDIP(12));
  dlg.SetSizerAndFit(top);
  dlg.CentreOnParent();
  dlg.ShowModal();
}

/* ------------------------------------------------------------------ Help */

void open_help()
{
  wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
  const wxString dirs[]= {exe.GetPath(), exe.GetPath()+"/../share/doc/wackget", "/usr/share/doc/wackget"};

  for(const wxString &d : dirs) {
    wxFileName f(d, "WackGet.html");

    if(f.FileExists()) {
      wxLaunchDefaultBrowser(wxFileName::FileNameToURL(f));
      return;
    }
  }
  wxLaunchDefaultBrowser("https://github.com/levoniust/WackGet#readme");
}

/* ------------------------------------------------------------------ Preferences (property sheet) */

namespace {

struct Prefs {
  /* General */
  wxTextCtrl *dir;
  wxSpinCtrl *threads;
  wxRadioButton *resume, *overwrite, *rm_delete, *rm_leave, *rm_prompt;
  wxCheckBox *clear_completed, *clear_failed, *log;
  /* Integration */
  wxCheckBox *assoc, *hotkey;
  /* Display */
  wxCheckBox *menubar, *statusbar, *headers, *grid, *traystatus, *ontop;
  wxRadioButton *show_taskbar, *show_tray, *show_both;
  wxChoice *skin, *dark;
  /* Proxy */
  wxCheckBox *proxy;
  wxTextCtrl *server, *user, *pass;
};

wxStaticBoxSizer *group(wxWindow *page, const wxString &title)
{
  return new wxStaticBoxSizer(wxVERTICAL, page, title);
}

wxPanel *general_page(wxWindow *book, Prefs &p)
{
  auto *page= new wxPanel(book);
  auto *sz= new wxBoxSizer(wxVERTICAL);
  int g= page->FromDIP(6);

  auto *loc= group(page, "Locations");
  wxWindow *lb= loc->GetStaticBox();
  loc->Add(new wxStaticText(lb, wxID_ANY, "Download &directory:"), 0, wxALL, g);
  auto *row= new wxBoxSizer(wxHORIZONTAL);
  p.dir= new wxTextCtrl(lb, wxID_ANY);
  p.dir->ChangeValue(wxString::FromUTF8(prefs::get_str("DownloadDir", "")));
  p.dir->SetHint(wxString::FromUTF8(prefs::download_dir()));
  auto *browse= new wxButton(lb, wxID_ANY, "...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
  browse->Bind(wxEVT_BUTTON, [page, &p](wxCommandEvent &) {
    wxDirDialog dd(page, "Select Download Directory...", p.dir->GetValue().empty()?
                   wxString::FromUTF8(prefs::download_dir()): p.dir->GetValue());
    if(dd.ShowModal()==wxID_OK) {
      p.dir->SetValue(dd.GetPath());
    }
  });
  row->Add(p.dir, 1, wxRIGHT|wxALIGN_CENTER_VERTICAL, g);
  row->Add(browse, 0, wxALIGN_CENTER_VERTICAL);
  loc->Add(row, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, g);
  sz->Add(loc, 0, wxEXPAND|wxALL, g);

  auto *qg= group(page, "Queue");
  wxWindow *qb= qg->GetStaticBox();
  row= new wxBoxSizer(wxHORIZONTAL);
  row->Add(new wxStaticText(qb, wxID_ANY, "Ma&ximum concurrent downloads (zero for no limit):"), 1,
           wxALIGN_CENTER_VERTICAL|wxRIGHT, g);
  p.threads= new wxSpinCtrl(qb, wxID_ANY, wxString(), wxDefaultPosition, wxSize(page->FromDIP(70), -1),
                            wxSP_ARROW_KEYS, 0, 999, prefs::get_int("MaxThreads", 0));
  row->Add(p.threads, 0, wxALIGN_CENTER_VERTICAL);
  qg->Add(row, 0, wxEXPAND|wxALL, g);

  row= new wxBoxSizer(wxHORIZONTAL);
  row->Add(new wxStaticText(qb, wxID_ANY, "If file exists:"), 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, g);
  p.resume= new wxRadioButton(qb, wxID_ANY, "&Resume", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  p.overwrite= new wxRadioButton(qb, wxID_ANY, "Over&write");
  (prefs::get_int("AutoResume", 1)? p.resume: p.overwrite)->SetValue(true);
  row->Add(p.resume, 0, wxRIGHT, g);
  row->Add(p.overwrite);
  qg->Add(row, 0, wxLEFT|wxRIGHT|wxBOTTOM, g);

  qg->Add(new wxStaticText(qb, wxID_ANY, "When removing unfinished items from queue:"), 0, wxLEFT|wxRIGHT, g);
  row= new wxBoxSizer(wxHORIZONTAL);
  p.rm_delete= new wxRadioButton(qb, wxID_ANY, "D&elete partial file", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  p.rm_leave= new wxRadioButton(qb, wxID_ANY, "Lea&ve partial file");
  p.rm_prompt= new wxRadioButton(qb, wxID_ANY, "&Prompt");
  int rm= prefs::get_int("RemoveDeletesPartial", -1);
  (rm>0? p.rm_delete: rm==0? p.rm_leave: p.rm_prompt)->SetValue(true);
  row->Add(p.rm_delete, 0, wxRIGHT, g);
  row->Add(p.rm_leave, 0, wxRIGHT, g);
  row->Add(p.rm_prompt);
  qg->Add(row, 0, wxALL, g);

  p.clear_completed= new wxCheckBox(qb, wxID_ANY, "Automatically &clear completed downloads");
  p.clear_completed->SetValue(prefs::get_int("ClearCompleted", 0)!=0);
  p.clear_failed= new wxCheckBox(qb, wxID_ANY, "Clear All Complete clears &failed downloads");
  p.clear_failed->SetValue(prefs::get_int("ClearFailedToo", 0)!=0);
  p.log= new wxCheckBox(qb, wxID_ANY, "&Log downloaded files (WackGet.log)");
  p.log->SetValue(prefs::get_int("Log", 0)!=0);
  p.log->SetToolTip(wxString::FromUTF8(prefs::dir()));
  qg->Add(p.clear_completed, 0, wxLEFT|wxRIGHT|wxBOTTOM, g);
  qg->Add(p.clear_failed, 0, wxLEFT|wxRIGHT|wxBOTTOM, g);
  qg->Add(p.log, 0, wxLEFT|wxRIGHT|wxBOTTOM, g);
  sz->Add(qg, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, g);
  page->SetSizer(sz);
  return page;
}

wxPanel *integration_page(wxWindow *book, Prefs &p, MainFrame *frame)
{
  auto *page= new wxPanel(book);
  auto *sz= new wxBoxSizer(wxVERTICAL);
  int g= page->FromDIP(6);

#ifdef __WXMSW__
  auto *wg= group(page, "Windows");
#else
  auto *wg= group(page, "Desktop");
#endif
  wxWindow *wb= wg->GetStaticBox();
  p.assoc= new wxCheckBox(wb, wxID_ANY, "A&ssociate .wgq files with WackGet");
  p.assoc->SetValue(platform_assoc_get());
  p.assoc->Enable(platform_assoc_supported());
  wg->Add(p.assoc, 0, wxALL, g);

  auto *row= new wxBoxSizer(wxHORIZONTAL);
  p.hotkey= new wxCheckBox(wb, wxID_ANY, "Use &hot key to toggle WackGet:");
  p.hotkey->SetValue(prefs::get_int("HotKey", 0)!=0);
  wxString hk= frame->hotkey_text();
#ifdef __WXMSW__
  wxString hk_default= "Win + W";
#else
  wxString hk_default= "Ctrl + Alt + W";
#endif
  auto *field= new wxStaticText(wb, wxID_ANY, hk.empty()? hk_default: hk, wxDefaultPosition,
                                wxSize(page->FromDIP(110), -1), wxALIGN_CENTRE_HORIZONTAL|wxBORDER_SUNKEN);
  row->Add(p.hotkey, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, g);
  row->Add(field, 0, wxALIGN_CENTER_VERTICAL);
  wg->Add(row, 0, wxLEFT|wxRIGHT|wxBOTTOM, g);
#if !wxUSE_HOTKEY && !defined(__WXGTK__)
  p.hotkey->Enable(false);
  field->Enable(false);
#endif
#ifdef __WXMSW__
  wg->Add(new wxStaticText(wb, wxID_ANY, "If Windows already uses Win + W (Widgets), WackGet uses Ctrl + Alt + W."),
          0, wxLEFT|wxRIGHT|wxBOTTOM, g);
#else
  if(!platform_assoc_supported()) {
    wg->Add(new wxStaticText(wb, wxID_ANY, "File association needs xdg-mime (package xdg-utils)."),
            0, wxLEFT|wxRIGHT|wxBOTTOM, g);
  }
  wg->Add(new wxStaticText(wb, wxID_ANY, "Ctrl + Alt + W on X11; on Wayland the desktop asks once and may\n"
                                         "let you pick another key. Or make a keyboard shortcut in your\n"
                                         "desktop settings that runs:  wackget --toggle"),
          0, wxLEFT|wxRIGHT|wxBOTTOM, g);
#endif
  sz->Add(wg, 0, wxEXPAND|wxALL, g);
  page->SetSizer(sz);
  return page;
}

wxPanel *display_page(wxWindow *book, Prefs &p)
{
  auto *page= new wxPanel(book);
  auto *sz= new wxBoxSizer(wxVERTICAL);
  int g= page->FromDIP(5);

  auto *vg= group(page, "View");
  wxWindow *vb= vg->GetStaticBox();
  auto check= [&](wxCheckBox *&cb, const char *label, const char *key, int dfl) {
    cb= new wxCheckBox(vb, wxID_ANY, label);
    cb->SetValue(prefs::get_int(key, dfl)!=0);
    vg->Add(cb, 0, wxLEFT|wxRIGHT|wxTOP, g);
  };
  check(p.menubar, "Show &menu bar", "ShowMenuBar", 1);
  check(p.statusbar, "Show &status bar", "ShowStatusBar", 1);
  check(p.headers, "Show column &headers", "ShowColumnHeaders", 1);
  check(p.grid, "Show &gridlines", "ShowGridLines", 1);
  check(p.traystatus, "Show stat&us in system tray", "TrayStatus", 1);
  check(p.ontop, "Always on &top (Ctrl+A)", "AlwaysOnTop", 0);
  vg->Add(new wxStaticText(vb, wxID_ANY, "Show icon in:"), 0, wxLEFT|wxRIGHT|wxTOP, g);
  p.show_taskbar= new wxRadioButton(vb, wxID_ANY, "Task&bar only", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  p.show_tray= new wxRadioButton(vb, wxID_ANY, "S&ystem tray only");
  p.show_both= new wxRadioButton(vb, wxID_ANY, "Taskbar a&nd system tray");
  int tb= prefs::get_int("ShowInTaskbar", 1), st= prefs::get_int("ShowInSystray", 1);
  (tb && !st? p.show_taskbar: !tb && st? p.show_tray: p.show_both)->SetValue(true);
  for(wxRadioButton *rb : {p.show_taskbar, p.show_tray, p.show_both}) {
    vg->Add(rb, 0, wxLEFT|wxTOP, page->FromDIP(22));
  }
  vg->AddSpacer(g);
  sz->Add(vg, 0, wxEXPAND|wxALL, g);

  auto *sg= group(page, "Skin");
  wxWindow *sb= sg->GetStaticBox();
  auto *grid= new wxFlexGridSizer(2, g, g*2);
  p.skin= new wxChoice(sb, wxID_ANY);
  for(int i= 0; i<(int)SkinId::Count; i++) {
    p.skin->Append(skin_label((SkinId)i));
  }
  p.skin->SetSelection((int)skin_from_name(prefs::get_str("Skin", "classic")));
  p.dark= new wxChoice(sb, wxID_ANY);
  p.dark->Append("Follow system");
  p.dark->Append("Light");
  p.dark->Append("Dark");
  p.dark->SetSelection(std::max(0, std::min(2, prefs::get_int("SkinDarkMode", 0))));
  grid->Add(new wxStaticText(sb, wxID_ANY, "&Look:"), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(p.skin);
  grid->Add(new wxStaticText(sb, wxID_ANY, "&Dark mode:"), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(p.dark);
  sg->Add(grid, 0, wxALL, g);
  auto *note= new wxStaticText(sb, wxID_ANY, "Classic uses the system's own list and status bar.");
  sg->Add(note, 0, wxLEFT|wxRIGHT|wxBOTTOM, g);
  auto sync= [&p]() { p.dark->Enable(p.skin->GetSelection()!=(int)SkinId::Classic); };
  sync();
  p.skin->Bind(wxEVT_CHOICE, [sync](wxCommandEvent &e) { sync(); e.Skip(); });
  sz->Add(sg, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, g);
  page->SetSizer(sz);
  return page;
}

wxPanel *proxy_page(wxWindow *book, Prefs &p)
{
  auto *page= new wxPanel(book);
  auto *sz= new wxBoxSizer(wxVERTICAL);
  int g= page->FromDIP(6);

  auto *pg= group(page, "Proxy");
  wxWindow *pb= pg->GetStaticBox();
  p.proxy= new wxCheckBox(pb, wxID_ANY, "Use pro&xy server");
  p.proxy->SetValue(prefs::get_int("Proxy", 0)!=0);
  pg->Add(p.proxy, 0, wxALL, g);
  auto *grid= new wxFlexGridSizer(2, g, g);
  grid->AddGrowableCol(1);
  p.server= new wxTextCtrl(pb, wxID_ANY);
  p.server->ChangeValue(wxString::FromUTF8(prefs::get_str("Proxy_Server", "")));
  p.server->SetHint("host:port");
  p.user= new wxTextCtrl(pb, wxID_ANY);
  p.user->ChangeValue(wxString::FromUTF8(prefs::get_str("Proxy_Username", "")));
  p.pass= new wxTextCtrl(pb, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
  p.pass->ChangeValue(wxString::FromUTF8(prefs::secret_load()));
  grid->Add(new wxStaticText(pb, wxID_ANY, "&Server:"), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(p.server, 1, wxEXPAND);
  grid->Add(new wxStaticText(pb, wxID_ANY, "&Username:"), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(p.user, 1, wxEXPAND);
  grid->Add(new wxStaticText(pb, wxID_ANY, "&Password:"), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(p.pass, 1, wxEXPAND);
  pg->Add(grid, 0, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, page->FromDIP(22));
#ifdef __WXMSW__
  const char *where= "The password is kept in Windows Credential Manager.";
#else
  const char *where= prefs::secret_available()? "The password is kept in the desktop keyring.":
                     "No keyring here: the password is kept only until WackGet exits.";
#endif
  pg->Add(new wxStaticText(pb, wxID_ANY, where), 0, wxLEFT|wxRIGHT|wxBOTTOM, g);
  sz->Add(pg, 0, wxEXPAND|wxALL, g);
  page->SetSizer(sz);
  return page;
}

void apply(Prefs &p, MainFrame *frame)
{
  /* General (on_general_apply) */
  prefs::set_str("DownloadDir", p.dir->GetValue().Trim().Trim(false).utf8_string());
  prefs::set_int("MaxThreads", p.threads->GetValue());
  prefs::set_int("AutoResume", p.resume->GetValue());
  prefs::set_int("ClearCompleted", p.clear_completed->GetValue());
  prefs::set_int("ClearFailedToo", p.clear_failed->GetValue());
  prefs::set_int("Log", p.log->GetValue());
  prefs::set_int("RemoveDeletesPartial", p.rm_delete->GetValue()? 1: p.rm_leave->GetValue()? 0: -1);
  /* Integration */
  if(p.assoc->IsEnabled() && p.assoc->GetValue()!=platform_assoc_get()) {
    platform_assoc_set(p.assoc->GetValue());
  }
  prefs::set_int("HotKey", p.hotkey->GetValue());
  /* Display */
  prefs::set_int("ShowMenuBar", p.menubar->GetValue());
  prefs::set_int("ShowStatusBar", p.statusbar->GetValue());
  prefs::set_int("ShowColumnHeaders", p.headers->GetValue());
  prefs::set_int("ShowGridLines", p.grid->GetValue());
  prefs::set_int("TrayStatus", p.traystatus->GetValue());
  prefs::set_int("AlwaysOnTop", p.ontop->GetValue());
  prefs::set_int("ShowInTaskbar", p.show_taskbar->GetValue() || p.show_both->GetValue());
  prefs::set_int("ShowInSystray", p.show_tray->GetValue() || p.show_both->GetValue());
  prefs::set_str("Skin", skin_name((SkinId)std::max(0, p.skin->GetSelection())));
  prefs::set_int("SkinDarkMode", std::max(0, p.dark->GetSelection()));
  /* Proxy */
  prefs::set_int("Proxy", p.proxy->GetValue());
  prefs::set_str("Proxy_Server", p.server->GetValue().Trim().Trim(false).utf8_string());
  prefs::set_str("Proxy_Username", p.user->GetValue().Trim().Trim(false).utf8_string());
  prefs::secret_store(p.user->GetValue().utf8_string(), p.pass->GetValue().utf8_string());
  prefs::save();
  frame->apply_prefs();
}

} // namespace

void run_prefs_dialog(MainFrame *frame)
{
  static wxPropertySheetDialog *open= nullptr;

  if(open) { /* already showing: bring it forward */
    open->Raise();
    return;
  }
  wxPropertySheetDialog dlg(frame, wxID_ANY, "WackGet Preferences");
  Prefs p;

  open= &dlg;
  dlg.SetIcons(frame->icons());
  dlg.CreateButtons(wxOK|wxCANCEL|wxAPPLY|wxHELP);
  wxBookCtrlBase *book= dlg.GetBookCtrl();
  book->AddPage(general_page(book, p), "General");
  book->AddPage(integration_page(book, p, frame), "Integration");
  book->AddPage(display_page(book, p), "Display");
  book->AddPage(proxy_page(book, p), "Proxy");
  dlg.LayoutDialog();

  wxWindow *apply_btn= dlg.FindWindow(wxID_APPLY);
  if(apply_btn) {
    apply_btn->Enable(false);
  }
  auto changed= [apply_btn](wxCommandEvent &e) {
    if(apply_btn) {
      apply_btn->Enable(true);
    }
    e.Skip();
  };
  dlg.Bind(wxEVT_CHECKBOX, changed);
  dlg.Bind(wxEVT_RADIOBUTTON, changed);
  dlg.Bind(wxEVT_TEXT, changed);
  dlg.Bind(wxEVT_CHOICE, changed);
  dlg.Bind(wxEVT_SPINCTRL, [apply_btn](wxSpinEvent &e) {
    if(apply_btn) {
      apply_btn->Enable(true);
    }
    e.Skip();
  });
  dlg.Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
    apply(p, frame);
    apply_btn->Enable(false);
  }, wxID_APPLY);
  dlg.Bind(wxEVT_BUTTON, [](wxCommandEvent &) { open_help(); }, wxID_HELP);
  dlg.CentreOnParent();
  if(dlg.ShowModal()==wxID_OK) {
    apply(p, frame);
  }
  open= nullptr;
}
