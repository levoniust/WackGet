/*
 * The main window: menu, queue list, status bar and tray icon, as in frame.c / frame_create.c.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#ifndef WACKGET_UI_FRAME_H
#define WACKGET_UI_FRAME_H

#include <string>
#include <vector>

#include <wx/bmpbndl.h>
#include <wx/frame.h>
#include <wx/iconbndl.h>
#include <wx/timer.h>

#include "queue.h"
#include "skin.h"
#include "views.h"

class Tray;
class SkinBar;

enum {
  ID_FILE_PREFERENCES= wxID_HIGHEST+1,
  ID_FILE_OPENDOWNLOADFOLDER,
  ID_ITEM_MOVEUP,
  ID_ITEM_MOVEDOWN,
  ID_ITEM_PAUSE,
  ID_ITEM_FORCE,
  ID_ITEM_REQUEUE,
  ID_ITEM_RUN,
  ID_QUEUE_CLEARALLCOMPLETE,
  ID_VIEW_ALWAYSONTOP,
  ID_EDIT_COPY_ALT,  /* Ctrl+Insert */
  ID_EDIT_PASTE_ALT, /* Shift+Insert */
};

class MainFrame : public wxFrame, public ViewOwner {
public:
  MainFrame();
  ~MainFrame() override;

  /* Command-line arguments (URLs and .wgq files), from this start or from a second WackGet
     (original process_command_line()). */
  void process_args(const std::vector<std::string> &args, bool always_append);
  void show_from_outside(); /* a second WackGet was started without arguments */
  void show_hide();         /* tray click / hot key (original do_showhide()) */
  void apply_prefs();       /* after Preferences OK/Apply */
  void exit_app();          /* File > Exit: may ask to save the queue */
  void show_initially();

  const wxIconBundle &icons() const { return icons_; }
  wxBitmapBundle idle_bundle() const;
  wxString hotkey_text() const { return hotkey_text_; }

  /* ViewOwner */
  void on_view_context_menu(bool on_item, const wxPoint &screen_pos) override;
  void on_view_activate() override;
  int on_view_drag(int drag_row, int over_row) override;

  /* Clipboard / drop parsing shared with the drop target. Returns how many URLs or queues were
     found; adds them unless just_query. */
  int paste(bool just_query);
  int add_from_text(const std::string &text, bool just_query);
  int add_from_html(const std::string &html, bool just_query);
  int add_from_files(const std::vector<std::string> &paths, bool just_query);

protected:
  void DoGiveHelp(const wxString &text, bool show) override; /* menu help in the skinned status bar */

private:
  wxMenuBar *make_menubar();
  wxMenu *make_item_menu();
  wxMenu *make_queue_menu();
  void build_view();
  void apply_skin();
  void apply_taskbar();
  void request_update();
  void update(); /* original on_timer_update() */
  std::vector<size_t> selection();
  void set_status_text(const wxString &s);
  bool might_save_queue();
  bool save_queue();
  bool load_queues(const std::vector<std::string> &paths, bool always_append);
  void register_hotkey();
  void save_window_state();
  void remember_normal_rect();

  void on_menu(wxCommandEvent &e);
  void on_update_ui(wxUpdateUIEvent &e);
  void on_close(wxCloseEvent &e);
  void on_timer(wxTimerEvent &);

  Queue q_;
  QueueView *view_= nullptr;
  SkinBar *bar_= nullptr;
  Tray *tray_= nullptr;
  wxMenuBar *menubar_= nullptr;
  wxTimer timer_;
  Skin skin_;
  wxIconBundle icons_;
  wxIcon idle_icon_, busy_icon_;
  bool tray_busy_= false;
  wxString idle_msg_, hotkey_text_;
  bool menu_open_= false, update_pending_= false, exiting_= false;
  wxRect normal_rect_;
  std::string proxy_pass_;
};

/* Run the dialogs (dialogs.cpp). */
bool run_add_item_dialog(wxWindow *parent, std::vector<std::string> &urls);
void run_about_dialog(MainFrame *parent);
void run_prefs_dialog(MainFrame *parent);
void open_help();

/* Platform bits (platform.cpp). */
bool platform_assoc_get();
bool platform_assoc_set(bool on);
bool platform_assoc_supported();
void platform_set_taskbar_button(wxFrame *f, bool show);
void platform_dark_title_bar(wxFrame *f, bool dark);

#endif
