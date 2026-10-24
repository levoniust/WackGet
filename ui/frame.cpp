/*
 * The main window. Menus, shortcuts and wording are the original's (wackget.rc); the handlers
 * follow frame.c, item_menu.c, queue_menu.c, list_drag.c, queue_save.c, droptarget.c and tray.c.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#include "frame.h"

#include <algorithm>
#include <cstring>

#include <wx/accel.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/imagbmp.h>
#include <wx/menu.h>
#include <wx/mstream.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statusbr.h>
#include <wx/taskbar.h>
#include <wx/time.h>
#include <wx/utils.h>

#include "icons.inc"
#include "prefs.h"

/* ------------------------------------------------------------------ skinned status bar */

class SkinBar : public wxWindow {
public:
  SkinBar(wxWindow *parent, const Skin &s): wxWindow(parent, wxID_ANY), skin_(s)
  {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, s.bar_h));
    Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
      wxAutoBufferedPaintDC dc(this);
      wxSize sz= GetClientSize();

      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetBrush(wxBrush(skin_.bar_bg));
      dc.DrawRectangle(0, 0, sz.x, sz.y);
      dc.SetPen(wxPen(skin_.bar_line, 1));
      dc.DrawLine(0, 0, sz.x, 0);
      dc.SetFont(skin_.bar_font);
      dc.SetTextForeground(skin_.bar_text);
      wxSize te= dc.GetTextExtent(text_.empty()? wxString("Ag"): text_);
      dc.DrawText(text_, skin_.pad, (sz.y-te.y)/2);
    });
  }
  void set(const wxString &t)
  {
    if(t!=text_) {
      text_= t;
      Refresh(false);
    }
  }

private:
  Skin skin_;
  wxString text_;
};

/* ------------------------------------------------------------------ tray icon */

class Tray : public wxTaskBarIcon {
public:
  explicit Tray(MainFrame *f): f_(f)
  {
    Bind(wxEVT_TASKBAR_LEFT_UP, [this](wxTaskBarIconEvent &) { f_->show_hide(); });
    Bind(wxEVT_MENU, [this](wxCommandEvent &e) { f_->GetEventHandler()->ProcessEvent(e); });
  }
  wxMenu *CreatePopupMenu() override /* IDM_TRAY */
  {
    wxMenu *m= new wxMenu;

    m->Append(wxID_NEW, "Add &New Item...");
    m->Append(wxID_PASTE, "Add From &Clipboard");
    m->Enable(wxID_PASTE, f_->paste(true)>0);
    m->AppendSeparator();
    m->Append(ID_FILE_PREFERENCES, "&Preferences...");
    m->Append(ID_FILE_OPENDOWNLOADFOLDER, "Open Download &Folder");
    m->AppendSeparator();
    m->Append(wxID_EXIT, "E&xit");
    return m;
  }

private:
  MainFrame *f_;
};

/* wxTaskBarIcon: Windows, and X11 desktops with an XEmbed tray */
class WxTray : public AppTray {
public:
  WxTray(MainFrame *f, const wxIcon &busy): tray_(new Tray(f)), idle_(f->idle_bundle()), busy_(busy)
  {
    tray_->SetIcon(idle_, "WackGet");
  }
  ~WxTray() override
  {
    tray_->RemoveIcon();
    tray_->Destroy();
  }
  void set(bool busy, const wxString &tip, bool) override
  {
    if(busy!=busy_on_ || tip!=tip_) {
      busy_on_= busy;
      tip_= tip;
      if(busy && busy_.IsOk()) {
        tray_->SetIcon(busy_, tip);
      } else {
        tray_->SetIcon(idle_, tip);
      }
    }
  }

private:
  Tray *tray_;
  wxBitmapBundle idle_;
  wxIcon busy_;
  bool busy_on_= false;
  wxString tip_= "WackGet";
};

/* ------------------------------------------------------------------ drag and drop (droptarget.c) */

class DropTarget : public wxDropTarget {
public:
  explicit DropTarget(MainFrame *f): f_(f)
  {
    auto *comp= new wxDataObjectComposite;

#ifdef __WXMSW__
    files_= new wxFileDataObject; /* HDROP first, then HTML, then text, like the original */
    comp->Add(files_, true);
    html_= new wxCustomDataObject(wxDataFormat("HTML Format")); /* raw CF_HTML, with SourceURL */
    comp->Add(html_);
#else
    html_= new wxHTMLDataObject;
    comp->Add(html_, true);
#endif
    text_= new wxTextDataObject;
    comp->Add(text_);
    comp_= comp;
    SetDataObject(comp);
  }

  wxDragResult OnDragOver(wxCoord, wxCoord, wxDragResult) override { return wxDragCopy; }

  wxDragResult OnData(wxCoord, wxCoord, wxDragResult) override
  {
    if(!GetData()) {
      return wxDragNone;
    }
    wxDataFormat fmt= comp_->GetReceivedFormat();
    int n= 0;

#ifdef __WXMSW__
    if(fmt==files_->GetFormat()) {
      std::vector<std::string> paths;

      for(const wxString &p : files_->GetFilenames()) {
        paths.push_back(p.utf8_string());
      }
      n= f_->add_from_files(paths, false);
    } else if(fmt==html_->GetFormat()) {
      const char *d= static_cast<const char *>(html_->GetData());
      n= f_->add_from_html(std::string(d, strnlen(d, html_->GetSize())), false);
    }
#else
    if(fmt==html_->GetFormat()) {
      n= f_->add_from_html(html_->GetHTML().utf8_string(), false);
    }
#endif
    if(n==0) { /* text, or HTML without links */
      n= f_->add_from_text(text_->GetText().utf8_string(), false);
    }
    return n>0? wxDragCopy: wxDragNone; /* never "move": the browser must keep its link */
  }

private:
  MainFrame *f_;
  wxDataObjectComposite *comp_;
#ifdef __WXMSW__
  wxFileDataObject *files_;
  wxCustomDataObject *html_;
#else
  wxHTMLDataObject *html_;
#endif
  wxTextDataObject *text_;
};

/* ------------------------------------------------------------------ icons */

static wxIcon load_icon(const unsigned char *data, size_t len, int index)
{
  wxMemoryInputStream in(data, len);
  wxImage img;
  wxIcon icon;

  if(img.LoadFile(in, wxBITMAP_TYPE_ICO, index)) {
    icon.CopyFromBitmap(wxBitmap(img));
  }
  return icon;
}

wxIconBundle wackget_icons()
{
  wxIconBundle icons;

  if(!wxImage::FindHandler(wxBITMAP_TYPE_ICO)) {
    wxImage::AddHandler(new wxICOHandler);
  }
  for(int i= 0; i<3; i++) {
    wxIcon ic= load_icon(wackget_ico, sizeof(wackget_ico), i);

    if(ic.IsOk()) {
      icons.AddIcon(ic);
    }
  }
  return icons;
}

wxIcon wackget_busy_icon()
{
  if(!wxImage::FindHandler(wxBITMAP_TYPE_ICO)) {
    wxImage::AddHandler(new wxICOHandler);
  }
  return load_icon(busy_ico, sizeof(busy_ico), 0);
}

/* ------------------------------------------------------------------ frame */

wxBitmapBundle MainFrame::idle_bundle() const
{
  return wxBitmapBundle::FromIconBundle(icons_); /* 16/32/48: sharp tray icon at any DPI */
}

static long frame_style()
{
  long style= wxDEFAULT_FRAME_STYLE;

  if(prefs::get_int("AlwaysOnTop", 0)) {
    style|= wxSTAY_ON_TOP;
  }
  if(!prefs::get_int("ShowInTaskbar", 1)) {
    style|= wxFRAME_NO_TASKBAR;
    style&= ~wxMINIMIZE_BOX; /* Disable minimize button if not in taskbar */
  }
  return style;
}

MainFrame::MainFrame(): wxFrame(nullptr, wxID_ANY, "WackGet", wxDefaultPosition, wxDefaultSize, frame_style())
{
  icons_= wackget_icons();
  SetIcons(icons_);
  busy_icon_= wackget_busy_icon();

  /* Window placement (LastFrame*), checked against the screens that exist now */
  wxRect r(prefs::get_int("LastFrameX", -1), prefs::get_int("LastFrameY", -1),
           prefs::get_int("LastFrameWidth", FromDIP(760)), prefs::get_int("LastFrameHeight", FromDIP(220)));
  r.width= std::max(r.width, FromDIP(200));
  r.height= std::max(r.height, FromDIP(100));
  SetSize(r.GetSize());
  if(r.x!=-1 && wxDisplay::GetFromPoint(r.GetTopLeft()+wxPoint(FromDIP(20), FromDIP(10)))!=wxNOT_FOUND) {
    Move(r.GetTopLeft());
  } else {
    Centre();
  }
  normal_rect_= GetRect();

  menubar_= make_menubar();
  if(prefs::get_int("ShowMenuBar", 1)) {
    SetMenuBar(menubar_);
  }

  /* Every shortcut also works with the menu bar hidden (the original used an accelerator table). */
  wxAcceleratorEntry acc[]= {
    {wxACCEL_CTRL, 'A', ID_VIEW_ALWAYSONTOP},     {wxACCEL_CTRL, 'C', wxID_COPY},
    {wxACCEL_CTRL, 'K', ID_QUEUE_CLEARALLCOMPLETE}, {wxACCEL_CTRL, 'N', wxID_NEW},
    {wxACCEL_CTRL, 'O', wxID_OPEN},               {wxACCEL_CTRL, 'S', wxID_SAVE},
    {wxACCEL_CTRL, 'P', ID_ITEM_PAUSE},           {wxACCEL_CTRL, WXK_RETURN, ID_ITEM_FORCE},
    {wxACCEL_CTRL, 'Q', ID_ITEM_REQUEUE},         {wxACCEL_CTRL, 'V', wxID_PASTE},
    {wxACCEL_NORMAL, WXK_DELETE, wxID_DELETE},    {wxACCEL_ALT, WXK_DOWN, ID_ITEM_MOVEDOWN},
    {wxACCEL_ALT, WXK_UP, ID_ITEM_MOVEUP},        {wxACCEL_CTRL, WXK_INSERT, ID_EDIT_COPY_ALT},
    {wxACCEL_SHIFT, WXK_INSERT, ID_EDIT_PASTE_ALT}, {wxACCEL_NORMAL, WXK_RETURN, ID_ITEM_RUN},
    {wxACCEL_NORMAL, WXK_F1, wxID_HELP},
  };
  /* Each key gets its own command id, not the menu item's: Windows drops an accelerator whose
     menu item is greyed, and wxMSW only re-checks greying when a menu opens, so Ctrl+V could stay
     dead after the clipboard was empty once. The rules are checked when the key is pressed. */
  for(auto &a : acc) {
    key_cmds_.push_back(a.GetCommand());
    a.Set(a.GetFlags(), a.GetKeyCode(), ID_KEY_FIRST+(int)key_cmds_.size()-1);
  }
  SetAcceleratorTable(wxAcceleratorTable((int)(sizeof(acc)/sizeof(acc[0])), acc));
  Bind(wxEVT_MENU, [this](wxCommandEvent &e) {
    size_t n= (size_t)(e.GetId()-ID_KEY_FIRST);
    if(n>=key_cmds_.size()) {
      return;
    }
    wxUpdateUIEvent u(key_cmds_[n]);
    on_update_ui(u);
    if(u.GetSetEnabled() && !u.GetEnabled()) {
      return; /* greyed right now: the key does nothing, as the menu item would */
    }
    wxCommandEvent cmd(wxEVT_MENU, key_cmds_[n]);
    cmd.SetEventObject(this);
    on_menu(cmd);
  }, ID_KEY_FIRST, ID_KEY_LAST);

  SetSizer(new wxBoxSizer(wxVERTICAL));
  apply_skin();

  if(prefs::get_int("ShowInSystray", 1)) {
    make_tray();
  }
  register_hotkey();
  proxy_pass_= prefs::secret_load();

  Bind(wxEVT_MENU, &MainFrame::on_menu, this);
  Bind(wxEVT_UPDATE_UI, &MainFrame::on_update_ui, this);
  Bind(wxEVT_CLOSE_WINDOW, &MainFrame::on_close, this);
  Bind(wxEVT_MENU_OPEN, [this](wxMenuEvent &e) { menu_open_= true; e.Skip(); });
  Bind(wxEVT_MENU_CLOSE, [this](wxMenuEvent &e) {
    menu_open_= false;
    set_status_text(idle_msg_);
    e.Skip();
  });
  Bind(wxEVT_SHOW, [this](wxShowEvent &e) { request_update(); e.Skip(); });      /* tray menu: Show/Hide */
  Bind(wxEVT_ACTIVATE, [this](wxActivateEvent &e) {
    if(e.GetActive() && paste_when_active_!=0) {
      bool fresh= wxGetLocalTimeMillis()-paste_when_active_<5000;
      paste_when_active_= 0;
      if(fresh) {
        CallAfter([this] { paste(false); });
      }
    }
    e.Skip();
  });
  Bind(wxEVT_ICONIZE, [this](wxIconizeEvent &e) { request_update(); e.Skip(); });
  Bind(wxEVT_SIZE, [this](wxSizeEvent &e) { remember_normal_rect(); e.Skip(); });
  Bind(wxEVT_MOVE, [this](wxMoveEvent &e) { remember_normal_rect(); e.Skip(); });
  Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent &e) { CallAfter([this] { apply_skin(); }); e.Skip(); });
  Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent &e) {
    if(skin_.id!=SkinId::Classic) {
      CallAfter([this] { apply_skin(); }); /* the system switched light/dark */
    }
    e.Skip();
  });
#if wxUSE_HOTKEY
  Bind(wxEVT_HOTKEY, [this](wxKeyEvent &) { toggle(); });
#endif
  timer_.Bind(wxEVT_TIMER, &MainFrame::on_timer, this);
  timer_.Start(1000); /* once per second, like TIMER_UPDATE */
  request_update();
}

MainFrame::~MainFrame()
{
  timer_.Stop();
  if(GetMenuBar()!=menubar_) {
    delete menubar_; /* detached ("Show menu bar" off) */
  }
  delete view_;
}

wxMenuBar *MainFrame::make_menubar()
{
  auto *mb= new wxMenuBar;
  auto *file= new wxMenu;

  file->Append(wxID_OPEN, "L&oad Queue\tCtrl+O", "Load saved queue from file");
  file->Append(wxID_SAVE, "&Save Queue\tCtrl+S", "Save current queue to file");
  file->AppendSeparator();
  file->Append(ID_FILE_PREFERENCES, "&Preferences...", "Customize application settings");
  file->Append(ID_FILE_OPENDOWNLOADFOLDER, "Open Download &Folder", "Open the download folder");
  file->AppendSeparator();
  file->Append(wxID_EXIT, "E&xit", "Quit the application");
  mb->Append(file, "&File");
  mb->Append(make_item_menu(), "&Item");
  mb->Append(make_queue_menu(), "&Queue");
  auto *help= new wxMenu;
  help->Append(wxID_ABOUT, "&About WackGet...", "Display application information, version number and copyright");
  help->AppendSeparator();
  help->Append(wxID_HELP, "&Help\tF1", "Get help on application functions");
  mb->Append(help, "&Help");
  return mb;
}

wxMenu *MainFrame::make_item_menu()
{
  auto *m= new wxMenu;

  m->Append(ID_ITEM_MOVEUP, "Move &Up\tAlt+Up", "Move selected download up one spot in the queue");
  m->Append(ID_ITEM_MOVEDOWN, "Move D&own\tAlt+Down", "Move selected download down one spot in the queue");
  m->AppendSeparator();
  m->AppendCheckItem(ID_ITEM_PAUSE, "&Pause\tCtrl+P", "Pause selected download");
  m->Append(wxID_DELETE, "Remove\t&Del", "Remove selected download; terminate download if active");
  m->AppendSeparator();
  m->Append(ID_ITEM_FORCE, "&Force\tCtrl+Enter", "Immediately force selected download; retry download if failed");
  m->Append(ID_ITEM_REQUEUE, "Re-&queue\tCtrl+Q", "Add selected item to the queue again");
  m->AppendSeparator();
  m->Append(ID_ITEM_RUN, "Ru&n\tEnter", "Run selected download with its default application");
  m->Append(wxID_COPY, "&Copy URL\tCtrl+C", "Copy selected URL to Clipboard");
  return m;
}

wxMenu *MainFrame::make_queue_menu()
{
  auto *m= new wxMenu;

  m->Append(wxID_NEW, "Add &New Item...\tCtrl+N", "Manually enter a new URL to download");
  m->Append(wxID_PASTE, "Add From &Clipboard\tCtrl+V", "Parse Clipboard for URLs and add to queue");
  m->AppendSeparator();
  m->Append(ID_QUEUE_CLEARALLCOMPLETE, "Clear All Complete\tCtrl+&K", "Prune all completed downloads");
  m->Append(wxID_SELECTALL, "Select &All", "Select all downloads");
  return m;
}

void MainFrame::build_view()
{
  if(view_) {
    view_->sync_selection_to_items();
    view_->save_columns();
    GetSizer()->Detach(view_->window());
    view_->window()->Destroy();
    delete view_;
  }
  view_= (skin_.id==SkinId::Classic)? create_native_view(this, q_, this): create_skin_view(this, q_, this, skin_);
  view_->drop_window()->SetDropTarget(new DropTarget(this));
  GetSizer()->Insert(0, view_->window(), 1, wxEXPAND);
  view_->refresh();
  view_->sync_selection_from_items();
}

void MainFrame::apply_skin()
{
  Freeze();
  skin_= make_skin(skin_from_name(prefs::get_str("Skin", "classic")), this);
  build_view();

  bool show_bar= prefs::get_int("ShowStatusBar", 1)!=0;
  if(bar_) {
    GetSizer()->Detach(bar_);
    bar_->Destroy();
    bar_= nullptr;
  }
  if(skin_.id==SkinId::Classic) {
    if(show_bar && !GetStatusBar()) {
      CreateStatusBar(1);
    } else if(!show_bar && GetStatusBar()) {
      wxStatusBar *sb= GetStatusBar();
      SetStatusBar(nullptr);
      sb->Destroy();
    }
  } else {
    if(GetStatusBar()) {
      wxStatusBar *sb= GetStatusBar();
      SetStatusBar(nullptr);
      sb->Destroy();
    }
    if(show_bar) {
      bar_= new SkinBar(this, skin_);
      GetSizer()->Add(bar_, 0, wxEXPAND);
    }
    SetBackgroundColour(skin_.bg);
  }
  if(skin_.id==SkinId::Classic) {
    SetBackgroundColour(wxNullColour);
  }
  set_status_text(idle_msg_);
  Layout();
  Thaw();
  platform_dark_title_bar(this, skin_.id!=SkinId::Classic && skin_.dark);
  view_->window()->SetFocus();
}

void MainFrame::apply_taskbar()
{
  platform_set_taskbar_button(this, prefs::get_int("ShowInTaskbar", 1)!=0);
}

void MainFrame::show_initially()
{
  int cmd= prefs::get_int("LastFrameShowCmd", 1); /* SW_HIDE 0, SW_SHOWNORMAL 1, SW_SHOWMINIMIZED 2, SW_SHOWMAXIMIZED 3 */

  if(cmd==0 && tray_) {
    return; /* it was hidden in the tray */
  }
  if(cmd==3) {
    Maximize();
  }
  Show();
  if(cmd==2 && prefs::get_int("ShowInTaskbar", 1)) {
    Iconize();
  }
  apply_taskbar();
}

void MainFrame::show_from_outside()
{
  if(IsIconized()) {
    Iconize(false);
  }
  Show();
  Raise();
  apply_taskbar();
}

void MainFrame::show_hide()
{
  if(IsShown() && !IsIconized()) {
    Hide();
  } else {
    show_from_outside();
  }
}

void MainFrame::toggle(int front)
{
  if(IsShown() && !IsIconized() && (front<0? IsActive(): front!=0)) {
    Hide();
  } else {
    show_from_outside();
  }
}

/* The StatusNotifier icon where the desktop shows those, else wxTaskBarIcon if there is a tray */
void MainFrame::make_tray()
{
  if(tray_) {
    return;
  }
#ifdef __WXGTK__
  tray_= platform_make_tray(this);
#endif
  if(!tray_ && wxTaskBarIcon::IsAvailable()) {
    tray_= new WxTray(this, busy_icon_);
  }
}

void MainFrame::remove_tray()
{
  delete tray_;
  tray_= nullptr;
}

void MainFrame::remember_normal_rect()
{
  if(IsShown() && !IsIconized() && !IsMaximized()) {
    normal_rect_= GetRect();
  }
}

void MainFrame::save_window_state()
{
  int cmd= 1;

  if(!IsShown()) {
    cmd= 0;
  } else if(IsIconized()) {
    cmd= 2;
  } else if(IsMaximized()) {
    cmd= 3;
  }
  prefs::set_int("LastFrameShowCmd", cmd);
  prefs::set_int("LastFrameX", normal_rect_.x);
  prefs::set_int("LastFrameY", normal_rect_.y);
  prefs::set_int("LastFrameWidth", normal_rect_.width);
  prefs::set_int("LastFrameHeight", normal_rect_.height);
  view_->save_columns();
}

void MainFrame::DoGiveHelp(const wxString &text, bool show)
{
  set_status_text((show && !text.empty())? text: idle_msg_);
}

void MainFrame::set_status_text(const wxString &s)
{
  if(bar_) {
    bar_->set(s);
  } else if(GetStatusBar() && GetStatusBar()->GetStatusText()!=s) {
    SetStatusText(s);
  }
}

void MainFrame::register_hotkey()
{
#if wxUSE_HOTKEY
  UnregisterHotKey(1);
  hotkey_text_.clear();
  if(!prefs::get_int("HotKey", 0)) {
    return;
  }
  if(RegisterHotKey(1, wxMOD_WIN, 'W')) {
    hotkey_text_= "Win + W";
  } else if(RegisterHotKey(1, wxMOD_CONTROL|wxMOD_ALT, 'W')) {
    hotkey_text_= "Ctrl + Alt + W"; /* Windows 11 keeps Win+W for Widgets */
  } else {
    wxMessageBox("Failed to register hot key", "Error", wxOK|wxICON_ERROR, this);
  }
#elif defined(__WXGTK__)
  bool on= prefs::get_int("HotKey", 0)!=0;
  if(on==hotkey_on_) {
    return; /* unchanged: on Wayland, binding again could make the desktop ask again */
  }
  hotkey_on_= on;
  hotkey_text_= platform_hotkey_start(this, on);
  if(on && hotkey_text_.empty()) {
    wxMessageBox("Failed to register hot key Ctrl + Alt + W: another program has it, or this desktop has no "
                 "shortcuts portal.\n\nYou can make a keyboard shortcut in your desktop settings that runs:\n"
                 "wackget --toggle", "Error", wxOK|wxICON_ERROR, this);
  }
#endif
}

/* ------------------------------------------------------------------ the once-a-second update */

void MainFrame::request_update()
{
  if(!update_pending_) {
    update_pending_= true;
    CallAfter([this] {
      update_pending_= false;
      update();
    });
  }
}

void MainFrame::on_timer(wxTimerEvent &)
{
  update();
}

void MainFrame::update()
{
  if(exiting_) {
    return;
  }
  QueuePrefs p;

  view_->sync_selection_to_items();
  p.download_dir= prefs::download_dir();
  p.max_threads= std::max(0, prefs::get_int("MaxThreads", 0));
  p.auto_resume= prefs::get_int("AutoResume", 1)!=0;
  p.clear_completed= prefs::get_int("ClearCompleted", 0)!=0;
  p.log= prefs::get_int("Log", 0)!=0;
  if(p.log) {
    char *lp= wg_fs_join(prefs::dir().c_str(), "WackGet.log");
    p.log_path= lp;
    wg_free(lp);
  }
  if(prefs::get_int("Proxy", 0)) {
    p.proxy= prefs::get_str("Proxy_Server", "");
    p.proxy_user= prefs::get_str("Proxy_Username", "");
    p.proxy_pass= proxy_pass_;
  }

  QueueTotals t= q_.update(p);
  view_->refresh();
  if(t.changed_rows) {
    view_->sync_selection_from_items();
  }

  char tip[128];
  wg_text_tooltip(tip, sizeof(tip), t.downloading, t.queued, t.speed);
  wxString tipw= wxString::FromUTF8(tip);
  if(tray_) {
    tray_->set(prefs::get_int("TrayStatus", 1) && t.downloading>0, tipw, IsShown() && !IsIconized());
  }
  if(tipw!=idle_msg_) {
    idle_msg_= tipw;
    if(!menu_open_) {
      set_status_text(idle_msg_);
    }
  }
}

/* ------------------------------------------------------------------ selection helpers */

std::vector<size_t> MainFrame::selection()
{
  std::vector<size_t> rows;

  view_->sync_selection_to_items();
  for(size_t i= 0; i<q_.size(); i++) {
    if(q_[i].selected) {
      rows.push_back(i);
    }
  }
  return rows;
}

void MainFrame::on_view_context_menu(bool on_item, const wxPoint &screen_pos)
{
  wxMenu *m= on_item? make_item_menu(): make_queue_menu(); /* item menu or list background menu */

  PopupMenu(m, ScreenToClient(screen_pos));
  delete m;
}

void MainFrame::on_view_activate()
{
  wxUpdateUIEvent ue(ID_ITEM_RUN);
  on_update_ui(ue);
  if(ue.GetEnabled()) {
    wxCommandEvent ce(wxEVT_MENU, ID_ITEM_RUN);
    on_menu(ce);
  }
}

int MainFrame::on_view_drag(int drag_row, int over_row)
{
  if(drag_row<0 || (size_t)drag_row>=q_.size()) {
    return -1;
  }
  q_[(size_t)drag_row].selected= true; /* on_begindrag() selected the dragged item */
  std::vector<size_t> rows= selection();
  int diff= over_row-drag_row;

  if(!q_.can_move(rows, diff)) {
    return drag_row;
  }
  q_.move(rows, diff);
  view_->refresh();
  view_->sync_selection_from_items();
  view_->set_focus_row(drag_row+diff);
  view_->ensure_visible(drag_row+diff);
  return drag_row+diff;
}

/* ------------------------------------------------------------------ menu state (update_*_menu) */

void MainFrame::on_update_ui(wxUpdateUIEvent &e)
{
  int id= e.GetId();
  int going= 0, paused= 0, failed= 0, done= 0, cnt= 0;

  switch(id) {
  case wxID_SAVE:
    e.Enable(q_.size()>0);
    return;
  case wxID_PASTE:
    e.Enable(paste(true)>0);
    return;
  case wxID_SELECTALL:
    e.Enable(q_.size()>0);
    return;
  case ID_QUEUE_CLEARALLCOMPLETE: {
    bool failed_too= prefs::get_int("ClearFailedToo", 0)!=0;
    bool any= false;

    for(size_t i= 0; i<q_.size() && !any; i++) {
      any= q_[i].status==WG_STAT_DONE || (failed_too && q_[i].status==WG_STAT_FAILED);
    }
    e.Enable(any);
    return;
  }
  case ID_ITEM_MOVEUP:
  case ID_ITEM_MOVEDOWN:
    e.Enable(q_.can_move(selection(), id==ID_ITEM_MOVEUP? -1: 1));
    return;
  case ID_ITEM_PAUSE:
  case wxID_DELETE:
  case ID_ITEM_FORCE:
  case ID_ITEM_REQUEUE:
  case ID_ITEM_RUN:
  case wxID_COPY:
    break;
  default:
    e.Skip();
    return;
  }
  for(size_t r : selection()) {
    cnt++;
    switch(q_[r].status) {
    case WG_STAT_GOING:
      going++;
      break;
    case WG_STAT_PAUSED:
      paused++;
      break;
    case WG_STAT_FAILED:
      failed++;
      break;
    case WG_STAT_DONE:
      done++;
      break;
    default:
      break;
    }
  }
  switch(id) {
  case ID_ITEM_PAUSE:
    e.Check(paused>0);
    e.Enable(paused>0? cnt==paused: (going>0 && cnt==going));
    break;
  case ID_ITEM_FORCE:
    e.Enable(cnt>0 && going==0 && done==0);
    break;
  case ID_ITEM_REQUEUE:
    e.Enable(cnt>0 && cnt==failed);
    break;
  case ID_ITEM_RUN:
    e.Enable(done>0 && cnt==done);
    break;
  default: /* Remove, Copy URL */
    e.Enable(cnt>0);
  }
}

/* ------------------------------------------------------------------ commands */

void MainFrame::on_menu(wxCommandEvent &e)
{
  int id= e.GetId();

  if(id>=ID_KEY_FIRST && id<=ID_KEY_LAST) {
    e.Skip(); /* a shortcut key: its own handler (constructor) checks the rules first */
    return;
  }

  /* Shortcuts reach here even when the menu item would be greyed: check the same rules. */
  switch(id) {
  case ID_ITEM_MOVEUP:
  case ID_ITEM_MOVEDOWN:
  case ID_ITEM_PAUSE:
  case ID_ITEM_FORCE:
  case ID_ITEM_REQUEUE:
  case ID_ITEM_RUN:
  case wxID_SAVE: {
    wxUpdateUIEvent ue(id);
    on_update_ui(ue);
    if(!ue.GetEnabled()) {
      return;
    }
    break;
  }
  }

  switch(id) {
  case wxID_OPEN: {
    wxFileDialog dlg(this, "Load Queue", wxString(), wxString(), "WackGet Queues (*.wgq)|*.wgq",
                     wxFD_OPEN|wxFD_FILE_MUST_EXIST|wxFD_MULTIPLE);
    if(dlg.ShowModal()==wxID_OK) {
      wxArrayString paths;
      std::vector<std::string> v;

      dlg.GetPaths(paths);
      for(const wxString &p : paths) {
        v.push_back(p.utf8_string());
      }
      load_queues(v, false);
    }
    break;
  }
  case wxID_SAVE:
    save_queue();
    break;
  case ID_FILE_PREFERENCES:
    run_prefs_dialog(this);
    break;
  case ID_FILE_OPENDOWNLOADFOLDER: {
    std::string d= prefs::download_dir();
    wg_fs_mkdirs(d.c_str());
    wxLaunchDefaultApplication(wxString::FromUTF8(d));
    break;
  }
  case ID_TRAY_SHOWHIDE:
    show_hide();
    break;
  case wxID_EXIT:
    exit_app();
    break;

  case ID_ITEM_MOVEUP:
  case ID_ITEM_MOVEDOWN: {
    std::vector<size_t> rows= selection();
    int diff= (id==ID_ITEM_MOVEUP)? -1: 1;

    q_.move(rows, diff);
    view_->refresh();
    view_->sync_selection_from_items();
    if(!rows.empty()) {
      int tail= (int)(diff>0? rows.back(): rows.front());
      view_->set_focus_row(tail);
      view_->ensure_visible(tail);
    }
    break;
  }
  case ID_ITEM_PAUSE:
    for(size_t r : selection()) {
      if(q_[r].status==WG_STAT_GOING) {
        q_[r].status= WG_STAT_PAUSED;
      } else if(q_[r].status==WG_STAT_PAUSED) {
        q_[r].status= WG_STAT_GOING;
      }
    }
    request_update();
    break;
  case wxID_DELETE: {
    std::vector<size_t> rows= selection();
    int partials= 0;
    bool del= false;

    if(rows.empty()) {
      break;
    }
    for(size_t r : rows) {
      partials+= q_[r].is_partial();
    }
    if(partials>0) { /* See if user wants to delete partials */
      int pref= prefs::get_int("RemoveDeletesPartial", -1);

      if(pref!=-1) {
        del= pref!=0;
      } else {
        int a= wxMessageBox(partials==1? "Delete partially downloaded file?": "Delete partially downloaded files?",
                            "Confirm", wxYES_NO|wxCANCEL|wxICON_QUESTION, this);
        if(a==wxCANCEL) {
          break;
        }
        del= a==wxYES;
      }
    }
    q_.remove(rows, del, prefs::download_dir());
    view_->refresh();
    view_->sync_selection_from_items();
    request_update();
    break;
  }
  case ID_ITEM_FORCE:
    for(size_t r : selection()) {
      if(q_[r].status!=WG_STAT_DONE) {
        q_[r].status= WG_STAT_GOING; /* Simply start download */
      }
    }
    request_update();
    break;
  case ID_ITEM_REQUEUE:
    for(size_t r : selection()) {
      if(q_[r].status==WG_STAT_FAILED) {
        q_[r].status= WG_STAT_QUEUED;
      }
    }
    request_update();
    break;
  case ID_ITEM_RUN:
    for(size_t r : selection()) {
      wxLaunchDefaultApplication(wxString::FromUTF8(q_[r].local_path(prefs::download_dir())));
    }
    break;
  case wxID_COPY:
  case ID_EDIT_COPY_ALT: {
    std::string s;

    for(size_t r : selection()) {
      if(!s.empty()) {
        s+= ' ';
      }
      s+= q_[r].url;
    }
    if(!s.empty()) {
      wxClipboardLocker lock;
      if(!!lock) {
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(s)));
        wxTheClipboard->Flush(); /* keep it after WackGet exits */
      }
    }
    break;
  }

  case wxID_NEW: {
    std::vector<std::string> urls;

    if(run_add_item_dialog(this, urls)) {
      for(const auto &u : urls) {
        q_.add(u);
      }
      view_->refresh();
      request_update();
    }
    break;
  }
  case wxID_PASTE:
  case ID_EDIT_PASTE_ALT:
    paste(false);
    break;
  case ID_TRAY_PASTE:
#ifdef __WXGTK__
    /* Wayland gives the clipboard only to the window with the focus: bring WackGet forward
       and paste once it has it (if that happens soon, so a later click can't surprise). */
    if(platform_clipboard_needs_focus() && !IsActive()) {
      paste_when_active_= wxGetLocalTimeMillis();
      show_from_outside();
      break;
    }
#endif
    paste(false);
    break;
  case ID_QUEUE_CLEARALLCOMPLETE: {
    bool failed_too= prefs::get_int("ClearFailedToo", 0)!=0;
    std::vector<size_t> rows;

    for(size_t i= 0; i<q_.size(); i++) {
      if(q_[i].status==WG_STAT_DONE || (failed_too && q_[i].status==WG_STAT_FAILED)) {
        rows.push_back(i);
      }
    }
    view_->sync_selection_to_items();
    q_.remove(rows, false, std::string());
    view_->refresh();
    view_->sync_selection_from_items();
    request_update();
    break;
  }
  case wxID_SELECTALL:
    for(size_t i= 0; i<q_.size(); i++) {
      q_[i].selected= true;
    }
    view_->sync_selection_from_items();
    break;

  case wxID_ABOUT:
    run_about_dialog(this);
    break;
  case wxID_HELP:
    open_help();
    break;
  case ID_VIEW_ALWAYSONTOP:
    prefs::set_int("AlwaysOnTop", !prefs::get_int("AlwaysOnTop", 0));
    prefs::save();
    SetWindowStyleFlag(prefs::get_int("AlwaysOnTop", 0)? GetWindowStyleFlag()|wxSTAY_ON_TOP:
                                                          GetWindowStyleFlag()&~wxSTAY_ON_TOP);
    break;
  default:
    e.Skip();
  }
}

/* ------------------------------------------------------------------ adding (paste, drop, command line) */

int MainFrame::add_from_text(const std::string &text, bool just_query)
{
  std::vector<std::string> urls= urls_from_text(text), queues;

  /* Dropped/pasted .wgq files arrive as paths or file:// URIs on Linux */
  size_t pos= 0;
  while(pos<text.size()) {
    size_t end= text.find_first_of("\r\n", pos);
    std::string line= text.substr(pos, end==std::string::npos? std::string::npos: end-pos);

    pos= (end==std::string::npos)? text.size(): end+1;
    while(!line.empty() && (line.back()==' ' || line.back()=='\t')) {
      line.pop_back();
    }
    if(line.size()>4 && wxString::FromUTF8(line.substr(line.size()-4)).CmpNoCase(".wgq")==0) {
      wxString p= wxString::FromUTF8(line);
      if(p.StartsWith("file:")) {
        p= wxFileName::URLToFileName(p).GetFullPath();
      }
      if(wxFileName::FileExists(p)) {
        queues.push_back(p.utf8_string());
      }
    }
  }
  if(just_query) {
    return (int)(urls.size()+queues.size());
  }
  if(!queues.empty() && !load_queues(queues, false)) {
    return (int)queues.size();
  }
  for(const auto &u : urls) {
    q_.add(u);
  }
  if(!urls.empty()) {
    view_->refresh();
    request_update();
  }
  return (int)(urls.size()+queues.size());
}

int MainFrame::add_from_html(const std::string &html, bool just_query)
{
  std::vector<std::string> urls= urls_from_html(html, nullptr);

  if(!just_query) {
    for(const auto &u : urls) {
      q_.add(u);
    }
    if(!urls.empty()) {
      view_->refresh();
      request_update();
    }
  }
  return (int)urls.size();
}

int MainFrame::add_from_files(const std::vector<std::string> &paths, bool just_query)
{
  std::vector<std::string> queues;

  for(const auto &p : paths) {
    if(p.size()>4 && wxString::FromUTF8(p.substr(p.size()-4)).CmpNoCase(".wgq")==0) {
      queues.push_back(p);
    }
  }
  if(!just_query && !queues.empty()) {
    load_queues(queues, false);
  }
  return (int)queues.size();
}

int MainFrame::paste(bool just_query)
{
  wxClipboardLocker lock;
  int n= 0;

  if(!lock) {
    return 0;
  }
#ifdef __WXMSW__
  wxDataFormat cf_html("HTML Format");
  if(wxTheClipboard->IsSupported(cf_html)) {
    wxCustomDataObject d(cf_html);

    if(wxTheClipboard->GetData(d) && d.GetSize()>0) {
      const char *p= static_cast<const char *>(d.GetData());
      n= add_from_html(std::string(p, strnlen(p, d.GetSize())), just_query);
    }
  }
#else
  if(wxTheClipboard->IsSupported(wxDF_HTML)) {
    wxHTMLDataObject d;

    if(wxTheClipboard->GetData(d)) {
      n= add_from_html(d.GetHTML().utf8_string(), just_query);
    }
  }
#endif
  if(n==0 && wxTheClipboard->IsSupported(wxDF_UNICODETEXT)) {
    wxTextDataObject d;

    if(wxTheClipboard->GetData(d)) {
      n= add_from_text(d.GetText().utf8_string(), just_query);
    }
  }
  return n;
}

void MainFrame::process_args(const std::vector<std::string> &args, bool always_append)
{
  std::vector<std::string> urls, queues;

  for(const auto &a : args) {
    if(wg_url_is_supported(a.c_str())) {
      urls.push_back(a);
    } else if(a.size()>4 && wxString::FromUTF8(a.substr(a.size()-4)).CmpNoCase(".wgq")==0) {
      queues.push_back(a);
    }
  }
  if(urls.empty() && queues.empty()) {
    return;
  }
  /* Add urls and queues to the list (queues first, as in process_command_line()) */
  if(!queues.empty() && !load_queues(queues, always_append)) {
    return;
  }
  for(const auto &u : urls) {
    q_.add(u);
  }
  view_->refresh();
  request_update();
}

/* ------------------------------------------------------------------ queue files */

static void add_loaded(const wg_queue_item *item, void *ctx)
{
  static_cast<std::vector<wg_queue_item> *>(ctx)->push_back({wg_strdup(item->url), item->status, item->flags});
}

bool MainFrame::load_queues(const std::vector<std::string> &paths, bool always_append)
{
  for(const auto &path : paths) {
    std::vector<wg_queue_item> items;
    wg_queue_report rep= {};
    int rv= wg_queue_load(path.c_str(), add_loaded, &items, &rep);

    if(rv!=WG_OK) {
      for(auto &it : items) {
        wg_free(it.url);
      }
      if(wxMessageBox(wxString::Format("Failed to load %s\n\nPress OK to continue or Cancel to abort.",
                                       wxString::FromUTF8(path)),
                      "Error", wxOK|wxCANCEL|wxICON_ERROR, this)==wxCANCEL) {
        return false;
      }
      continue;
    }
    /* Check whether should append or not */
    if(!always_append) {
      if(q_.size()<1) {
        always_append= true; /* If current queue is empty, append everything */
      } else {
        int a= wxMessageBox("Append to current queue?", "Confirm", wxYES_NO|wxCANCEL|wxICON_QUESTION, this);

        if(a==wxCANCEL) {
          for(auto &it : items) {
            wg_free(it.url);
          }
          return false;
        }
        if(a==wxNO) {
          q_.clear();
        }
        always_append= true;
      }
    }
    for(auto &it : items) {
      q_.add(it.url, it.status, it.flags);
      wg_free(it.url);
    }
  }
  view_->refresh();
  view_->sync_selection_from_items();
  request_update();
  return true;
}

bool MainFrame::save_queue()
{
  wxFileDialog dlg(this, "Save Current Queue", wxString(), "*.wgq", "WackGet Queues (*.wgq)|*.wgq",
                   wxFD_SAVE|wxFD_OVERWRITE_PROMPT);

  if(dlg.ShowModal()!=wxID_OK) {
    return false;
  }
  wxString path= dlg.GetPath();
  if(!path.Lower().EndsWith(".wgq")) {
    path+= ".wgq";
  }
  std::vector<wg_queue_item> items= q_.snapshot_for_save();
  if(wg_queue_save(path.utf8_str(), items.data(), items.size())!=WG_OK) {
    wxMessageBox(wxString::Format("Failed to create %s", path), "Error", wxOK|wxICON_ERROR, this);
    return false;
  }
  return true;
}

bool MainFrame::might_save_queue()
{
  update(); /* Make sure list is updated */
  for(size_t i= 0; i<q_.size(); i++) {
    switch(q_[i].status) {
    case WG_STAT_QUEUED:
    case WG_STAT_GOING:
    case WG_STAT_PAUSED:
      return true;
    default:
      break;
    }
  }
  return false;
}

/* ------------------------------------------------------------------ preferences, exit */

void MainFrame::apply_prefs()
{
  SetWindowStyleFlag(prefs::get_int("AlwaysOnTop", 0)? GetWindowStyleFlag()|wxSTAY_ON_TOP:
                                                        GetWindowStyleFlag()&~wxSTAY_ON_TOP);
  SetMenuBar(prefs::get_int("ShowMenuBar", 1)? menubar_: nullptr);

  /* No tray on this desktop (e.g. GNOME without the AppIndicator extension): behave as if the
     tray were off, so closing the window never hides it somewhere unreachable. */
  if(prefs::get_int("ShowInSystray", 1)) {
    make_tray();
  } else if(tray_) {
    remove_tray();
  }
  if(!tray_) {
    if(!IsShown()) {
      show_from_outside(); /* never leave the window unreachable */
    }
  }
  apply_taskbar();
  register_hotkey();
  proxy_pass_= prefs::secret_load();

  apply_skin(); /* skin, status bar, column headers, grid lines */
  request_update();
}

void MainFrame::exit_app()
{
  /* Prompt to save queue if unfinished */
  if(might_save_queue()) {
    if(!IsShown()) {
      show_from_outside();
    }
    int a= wxMessageBox("Save current queue before exiting?", "Confirm", wxYES_NO|wxCANCEL|wxICON_QUESTION, this);

    if(a==wxCANCEL) {
      return;
    }
    if(a==wxYES && !save_queue()) {
      return; /* If saving of queue failed, don't exit */
    }
  }
  save_window_state();
  prefs::save();
  exiting_= true;
  timer_.Stop();
  q_.shutdown();
  remove_tray();
  Destroy();
}

void MainFrame::on_close(wxCloseEvent &e)
{
  if(exiting_) {
    e.Skip();
    return;
  }
  if(!e.CanVeto()) { /* logging off / shutting down: save what we can, no questions */
    save_window_state();
    prefs::save();
    exiting_= true;
    q_.shutdown();
    remove_tray();
    e.Skip();
    return;
  }
  e.Veto();
  if(tray_) { /* Close means hide if tray icon exists */
    Hide();
  } else {
    exit_app();
  }
}
