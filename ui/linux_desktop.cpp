/*
 * The Linux desktop:
 *  - tray icon through StatusNotifierItem (KDE, GNOME with the AppIndicator extension, Xfce,
 *    Cinnamon...), using libayatana-appindicator3 loaded at run time so WackGet still starts
 *    without it; the frame falls back to wxTaskBarIcon (XEmbed) or to no tray at all;
 *  - the hot key: an X11 key grab, or the desktop's GlobalShortcuts portal on Wayland;
 *  - the .wgq association: freedesktop MIME and .desktop files in the user's home (original
 *    add_file_assoc()).
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#include <dlfcn.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#endif

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/imagpng.h>
#include <wx/sstream.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/wfstream.h>

#include "frame.h"
#include "prefs.h"

static const char app_id[]= "io.github.levoniust.WackGet";
static const char queue_mime[]= "application/x-wackget-queue";

void platform_init_app()
{
  /* Wayland app_id and X11 WM_CLASS: lets the desktop match the window to its .desktop file */
  g_set_prgname(app_id);
  gdk_set_program_class("WackGet");
}

bool platform_clipboard_needs_focus()
{
#ifdef GDK_WINDOWING_WAYLAND
  return GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default());
#else
  return false;
#endif
}

/* ------------------------------------------------------------------ helpers */

static wxString data_home()
{
  wxString d;

  if(!wxGetEnv("XDG_DATA_HOME", &d) || d.empty()) {
    d= wxGetHomeDir()+"/.local/share";
  }
  return d;
}

static wxString config_home()
{
  wxString d;

  if(!wxGetEnv("XDG_CONFIG_HOME", &d) || d.empty()) {
    d= wxGetHomeDir()+"/.config";
  }
  return d;
}

/* An installed (system) data file, e.g. "applications/io.github.levoniust.WackGet.desktop" */
static bool system_data_file(const wxString &rel)
{
  wxString dirs;

  if(!wxGetEnv("XDG_DATA_DIRS", &dirs) || dirs.empty()) {
    dirs= "/usr/local/share:/usr/share";
  }
  for(const wxString &d : wxSplit(dirs, ':', '\0')) {
    if(!d.empty() && wxFileExists(d+"/"+rel)) {
      return true;
    }
  }
  return false;
}

static bool in_path(const char *prog)
{
  wxString path;

  wxGetEnv("PATH", &path);
  for(const wxString &d : wxSplit(path, ':', '\0')) {
    if(!d.empty() && wxFileName::IsFileExecutable(d+"/"+prog)) {
      return true;
    }
  }
  return false;
}

static bool write_file(const wxString &path, const wxString &text)
{
  wxFileName::Mkdir(wxFileName(path).GetPath(), 0755, wxPATH_MKDIR_FULL);
  wxFileOutputStream out(path);
  wxScopedCharBuffer u= text.utf8_str();

  return out.IsOk() && out.WriteAll(u.data(), u.length()) && out.Close();
}

static wxImage icon_image(const wxIcon &icon, int size)
{
  wxBitmap b;

  if(!wxImage::FindHandler(wxBITMAP_TYPE_PNG)) {
    wxImage::AddHandler(new wxPNGHandler); /* the PNGs for the panel and the icon theme */
  }

  b.CopyFromIcon(icon);
  wxImage img= b.ConvertToImage();
  if(img.IsOk() && img.GetWidth()!=size) {
    img.Rescale(size, size, wxIMAGE_QUALITY_NEAREST); /* pixel art: keep it crisp */
  }
  return img;
}

/* ------------------------------------------------------------------ tray (StatusNotifierItem) */

namespace {

/* The few libayatana-appindicator3 calls WackGet needs (app-indicator.h) */
struct Ayatana {
  void *(*new_with_path)(const char *id, const char *icon, int category, const char *theme_path);
  void (*set_status)(void *ind, int status);
  void (*set_menu)(void *ind, GtkMenu *menu);
  void (*set_icon_full)(void *ind, const char *icon, const char *desc);
  void (*set_title)(void *ind, const char *title);
  void (*set_secondary_activate_target)(void *ind, GtkWidget *item);

  bool load()
  {
    /* It warns on stderr that the GTK-free libayatana-appindicator-glib is newer; Debian 13
       does not ship that one yet. */
    g_log_set_handler("libayatana-appindicator", G_LOG_LEVEL_WARNING,
                      [](const gchar *, GLogLevelFlags, const gchar *, gpointer) {}, nullptr);
    void *h= dlopen("libayatana-appindicator3.so.1", RTLD_NOW|RTLD_LOCAL);

    if(!h) {
      return false;
    }
    new_with_path= (decltype(new_with_path))dlsym(h, "app_indicator_new_with_path");
    set_status= (decltype(set_status))dlsym(h, "app_indicator_set_status");
    set_menu= (decltype(set_menu))dlsym(h, "app_indicator_set_menu");
    set_icon_full= (decltype(set_icon_full))dlsym(h, "app_indicator_set_icon_full");
    set_title= (decltype(set_title))dlsym(h, "app_indicator_set_title");
    set_secondary_activate_target= (decltype(set_secondary_activate_target))dlsym(
        h, "app_indicator_set_secondary_activate_target");
    return new_with_path && set_status && set_menu && set_icon_full && set_title;
  }
};
enum { CATEGORY_APPLICATION_STATUS= 0, STATUS_ACTIVE= 1 };

/* Is something showing StatusNotifierItems? (Plasma, the GNOME AppIndicator extension, ...) */
bool sni_host_present()
{
  GDBusConnection *bus= g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  bool ok= false;

  if(!bus) {
    return false;
  }
  GVariant *r= g_dbus_connection_call_sync(
      bus, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.freedesktop.DBus.Properties", "Get",
      g_variant_new("(ss)", "org.kde.StatusNotifierWatcher", "IsStatusNotifierHostRegistered"),
      G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, nullptr, nullptr);
  if(r) {
    GVariant *v= nullptr;

    g_variant_get(r, "(v)", &v);
    ok= v && g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(v);
    if(v) {
      g_variant_unref(v);
    }
    g_variant_unref(r);
  }
  g_object_unref(bus);
  return ok;
}

class IndicatorTray : public AppTray {
public:
  IndicatorTray(MainFrame *f, const Ayatana &a): f_(f), a_(a) {}

  bool create()
  {
    /* The indicator takes icons by name from a folder: write ours where only this user can see them */
    wxString dir;
    if(!wxGetEnv("XDG_RUNTIME_DIR", &dir) || dir.empty()) {
      dir= wxString::FromUTF8(prefs::dir());
    }
    dir+= "/wackget-tray";
    wxFileName::Mkdir(dir, 0700, wxPATH_MKDIR_FULL);
    wxImage idle= icon_image(f_->icons().GetIcon(wxSize(48, 48)), 48);
    wxImage busy= icon_image(wackget_busy_icon(), 48);
    if(!idle.IsOk() || !idle.SaveFile(dir+"/wackget-idle.png", wxBITMAP_TYPE_PNG)) {
      return false;
    }
    if(busy.IsOk()) {
      busy.SaveFile(dir+"/wackget-busy.png", wxBITMAP_TYPE_PNG);
    }

    ind_= a_.new_with_path(app_id, "wackget-idle", CATEGORY_APPLICATION_STATUS, dir.utf8_str());
    if(!ind_) {
      return false;
    }
    menu_= gtk_menu_new();
    g_object_ref_sink(menu_);
    show_= add("Hide WackGet", ID_TRAY_SHOWHIDE); /* no left click with indicators: the menu says it */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_), gtk_separator_menu_item_new());
    /* IDM_TRAY */
    add("Add _New Item...", wxID_NEW);
    add("Add From _Clipboard", ID_TRAY_PASTE); /* always on: on Wayland it can't be read from here */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_), gtk_separator_menu_item_new());
    add("_Preferences...", ID_FILE_PREFERENCES);
    add("Open Download _Folder", ID_FILE_OPENDOWNLOADFOLDER);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_), gtk_separator_menu_item_new());
    add("E_xit", wxID_EXIT);
    gtk_widget_show_all(menu_);
    a_.set_menu(ind_, GTK_MENU(menu_));
    if(a_.set_secondary_activate_target) {
      a_.set_secondary_activate_target(ind_, show_); /* middle click */
    }
    a_.set_title(ind_, "WackGet");
    a_.set_status(ind_, STATUS_ACTIVE);
    return true;
  }

  ~IndicatorTray() override
  {
    if(ind_) {
      g_object_unref(ind_); /* unregisters from the panel */
    }
    if(menu_) {
      gtk_widget_destroy(menu_);
      g_object_unref(menu_);
    }
  }

  void set(bool busy, const wxString &tip, bool shown) override
  {
    if(busy!=busy_) {
      busy_= busy;
      a_.set_icon_full(ind_, busy? "wackget-busy": "wackget-idle", "WackGet");
    }
    if(tip!=tip_) {
      tip_= tip;
      a_.set_title(ind_, tip.utf8_str()); /* the tooltip in Plasma */
    }
    if(shown!=shown_) {
      shown_= shown;
      gtk_menu_item_set_label(GTK_MENU_ITEM(show_), shown? "Hide WackGet": "Show WackGet");
    }
  }

private:
  GtkWidget *add(const char *label, int id)
  {
    GtkWidget *item= gtk_menu_item_new_with_mnemonic(label);

    g_object_set_data(G_OBJECT(item), "wackget-id", GINT_TO_POINTER(id));
    g_signal_connect(item, "activate", G_CALLBACK(+[](GtkMenuItem *mi, gpointer self) {
                       auto *t= (IndicatorTray *)self;
                       wxCommandEvent ev(wxEVT_MENU, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(mi), "wackget-id")));
                       t->f_->GetEventHandler()->AddPendingEvent(ev);
                     }),
                     this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_), item);
    return item;
  }

  MainFrame *f_;
  Ayatana a_;
  void *ind_= nullptr;
  GtkWidget *menu_= nullptr, *show_= nullptr;
  bool busy_= false, shown_= true;
  wxString tip_;
};

} // namespace

AppTray *platform_make_tray(MainFrame *f)
{
  static Ayatana a;
  static bool loaded= a.load();

  if(!loaded || !sni_host_present()) {
    return nullptr;
  }
  auto *t= new IndicatorTray(f, a);
  if(!t->create()) {
    delete t;
    return nullptr;
  }
  return t;
}

/* ------------------------------------------------------------------ hot key */

namespace {

class HotKey {
public:
  virtual ~HotKey()= default;
};

#ifdef GDK_WINDOWING_X11
/* X11: grab Ctrl+Alt+W on the root window, whatever Num Lock and Caps Lock are doing */
class X11HotKey : public HotKey {
public:
  explicit X11HotKey(MainFrame *f): f_(f) {}

  bool grab()
  {
    GdkDisplay *gd= gdk_display_get_default();

    dpy_= GDK_DISPLAY_XDISPLAY(gd);
    root_= DefaultRootWindow(dpy_);
    code_= XKeysymToKeycode(dpy_, XK_w);
    if(!code_) {
      return false;
    }
    gdk_x11_display_error_trap_push(gd);
    for(unsigned extra : extras) {
      XGrabKey(dpy_, code_, mods|extra, root_, False, GrabModeAsync, GrabModeAsync);
    }
    if(gdk_x11_display_error_trap_pop(gd)) { /* BadAccess: another program has it */
      ungrab();
      return false;
    }
    gdk_window_add_filter(nullptr, filter, this);
    filtering_= true;
    return true;
  }

  ~X11HotKey() override
  {
    if(filtering_) {
      gdk_window_remove_filter(nullptr, filter, this);
      ungrab();
    }
  }

private:
  static constexpr unsigned mods= ControlMask|Mod1Mask;
  static constexpr unsigned extras[]= {0, LockMask, Mod2Mask, LockMask|Mod2Mask};

  void ungrab()
  {
    GdkDisplay *gd= gdk_display_get_default();

    gdk_x11_display_error_trap_push(gd);
    for(unsigned extra : extras) {
      XUngrabKey(dpy_, code_, mods|extra, root_);
    }
    gdk_x11_display_error_trap_pop_ignored(gd);
  }

  static GdkFilterReturn filter(GdkXEvent *xev, GdkEvent *, gpointer self)
  {
    auto *h= (X11HotKey *)self;
    XEvent *e= (XEvent *)xev;

    if(e->type==KeyPress && e->xkey.keycode==h->code_ && (e->xkey.state&(ControlMask|Mod1Mask|ShiftMask|Mod4Mask))==mods) {
      int front= h->in_front();
      h->f_->CallAfter([f= h->f_, front] { f->toggle(front); });
      return GDK_FILTER_REMOVE;
    }
    return GDK_FILTER_CONTINUE;
  }

  /* The grab itself takes the focus away from our window for a moment, so ask the window
     manager what is in front instead (-1: no window manager says, use the focus). */
  int in_front()
  {
    Atom prop= XInternAtom(dpy_, "_NET_ACTIVE_WINDOW", True), type;
    int format, front= -1;
    unsigned long n, left;
    unsigned char *data= nullptr;
    GdkWindow *gw= gtk_widget_get_window(f_->GetHandle());

    if(prop==None || !gw || !GDK_IS_X11_WINDOW(gw)) {
      return -1;
    }
    if(XGetWindowProperty(dpy_, root_, prop, 0, 1, False, XA_WINDOW, &type, &format, &n, &left, &data)==Success &&
       data) {
      if(type==XA_WINDOW && format==32 && n==1) {
        front= *(::Window *)data==GDK_WINDOW_XID(gw)? 1: 0;
      }
      XFree(data);
    }
    return front;
  }

  MainFrame *f_;
  Display *dpy_= nullptr;
  ::Window root_= 0;
  unsigned code_= 0;
  bool filtering_= false;
};
#endif

/* Wayland: org.freedesktop.portal.GlobalShortcuts (GNOME 48+, Plasma 6). The desktop asks the
   user once and may let them pick another key; what it settled on comes back as text. */
class PortalHotKey : public HotKey {
public:
  explicit PortalHotKey(MainFrame *f): f_(f) {}

  static bool available(GDBusConnection *bus)
  {
    GVariant *r= g_dbus_connection_call_sync(
        bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.DBus.Properties",
        "Get", g_variant_new("(ss)", "org.freedesktop.portal.GlobalShortcuts", "version"), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);

    if(!r) {
      return false;
    }
    g_variant_unref(r);
    return true;
  }

  void start(GDBusConnection *bus)
  {
    bus_= (GDBusConnection *)g_object_ref(bus);
    activated_= g_dbus_connection_signal_subscribe(bus_, "org.freedesktop.portal.Desktop",
                                                   "org.freedesktop.portal.GlobalShortcuts", "Activated",
                                                   "/org/freedesktop/portal/desktop", nullptr,
                                                   G_DBUS_SIGNAL_FLAGS_NONE, on_activated, this, nullptr);
    GVariantBuilder opt;
    g_variant_builder_init(&opt, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&opt, "{sv}", "handle_token", g_variant_new_string(expect("wackget_session")));
    g_variant_builder_add(&opt, "{sv}", "session_handle_token", g_variant_new_string("wackget"));
    call("CreateSession", g_variant_new("(a{sv})", &opt));
  }

  ~PortalHotKey() override
  {
    if(!bus_) {
      return;
    }
    if(response_) {
      g_dbus_connection_signal_unsubscribe(bus_, response_);
    }
    if(activated_) {
      g_dbus_connection_signal_unsubscribe(bus_, activated_);
    }
    if(!session_.empty()) {
      g_dbus_connection_call(bus_, "org.freedesktop.portal.Desktop", session_.c_str(),
                             "org.freedesktop.portal.Session", "Close", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE,
                             -1, nullptr, nullptr, nullptr);
    }
    g_dbus_connection_close_sync(bus_, nullptr, nullptr); /* ours alone: unref would leave it open */
    g_object_unref(bus_);
  }

private:
  /* Portal replies come as a Response signal on a request object whose path we can predict */
  const char *expect(const char *token)
  {
    std::string sender= g_dbus_connection_get_unique_name(bus_)+1; /* ":1.42" -> "1_42" */

    std::replace(sender.begin(), sender.end(), '.', '_');
    if(response_) {
      g_dbus_connection_signal_unsubscribe(bus_, response_);
    }
    token_= token;
    request_= "/org/freedesktop/portal/desktop/request/"+sender+"/"+token;
    response_= g_dbus_connection_signal_subscribe(bus_, "org.freedesktop.portal.Desktop",
                                                  "org.freedesktop.portal.Request", "Response", request_.c_str(),
                                                  nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_response, this, nullptr);
    return token_.c_str();
  }

  void call(const char *method, GVariant *args)
  {
    g_dbus_connection_call(bus_, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                           "org.freedesktop.portal.GlobalShortcuts", method, args, nullptr, G_DBUS_CALL_FLAGS_NONE,
                           -1, nullptr, nullptr, nullptr);
  }

  void bind()
  {
    GVariantBuilder sc, props, opt;

    g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&props, "{sv}", "description", g_variant_new_string("Show or hide WackGet"));
    g_variant_builder_add(&props, "{sv}", "preferred_trigger", g_variant_new_string("CTRL+ALT+w"));
    g_variant_builder_init(&sc, G_VARIANT_TYPE("a(sa{sv})"));
    g_variant_builder_add(&sc, "(sa{sv})", "toggle", &props);
    g_variant_builder_init(&opt, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&opt, "{sv}", "handle_token", g_variant_new_string(expect("wackget_bind")));
    call("BindShortcuts", g_variant_new("(oa(sa{sv})sa{sv})", session_.c_str(), &sc, "", &opt));
  }

  static void on_response(GDBusConnection *, const char *, const char *, const char *, const char *,
                          GVariant *params, gpointer self)
  {
    auto *h= (PortalHotKey *)self;
    guint32 code= 2;
    GVariant *results= nullptr;

    g_variant_get(params, "(u@a{sv})", &code, &results);
    if(h->token_=="wackget_session") {
      const char *s= nullptr;

      if(code==0 && (g_variant_lookup(results, "session_handle", "&s", &s) ||
                     g_variant_lookup(results, "session_handle", "&o", &s))) {
        h->session_= s;
        h->bind();
      } else {
        h->f_->set_hotkey_text(wxString());
      }
    } else if(h->token_=="wackget_bind") {
      wxString text;
      GVariantIter *it= nullptr;

      if(code==0 && g_variant_lookup(results, "shortcuts", "a(sa{sv})", &it)) {
        const char *id;
        GVariant *props;

        while(g_variant_iter_loop(it, "(&s@a{sv})", &id, &props)) {
          const char *trig= nullptr;

          if(g_variant_lookup(props, "trigger_description", "&s", &trig) && trig && *trig) {
            text= wxString::FromUTF8(trig);
          }
        }
        g_variant_iter_free(it);
        if(text.empty()) {
          text= "set by the desktop";
        }
      }
      h->f_->set_hotkey_text(text);
    }
    g_variant_unref(results);
  }

  static void on_activated(GDBusConnection *, const char *, const char *, const char *, const char *,
                           GVariant *params, gpointer self)
  {
    auto *h= (PortalHotKey *)self;
    const char *session, *id;

    g_variant_get_child(params, 0, "&o", &session);
    g_variant_get_child(params, 1, "&s", &id);
    if(h->session_==session && strcmp(id, "toggle")==0) {
      h->f_->CallAfter([f= h->f_] { f->toggle(); });
    }
  }

  MainFrame *f_;
  GDBusConnection *bus_= nullptr;
  guint response_= 0, activated_= 0;
  std::string token_, request_, session_;
};

std::unique_ptr<HotKey> hotkey;

} // namespace

wxString platform_hotkey_start(MainFrame *f, bool on)
{
  hotkey.reset();
  if(!on) {
    return wxString();
  }
#ifdef GDK_WINDOWING_X11
  if(GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
    auto *h= new X11HotKey(f);

    if(h->grab()) {
      hotkey.reset(h);
      return "Ctrl + Alt + W";
    }
    delete h;
    return wxString();
  }
#endif
  /* A connection of our own: the portal fixes a connection's app id at its first request, and
     GTK has already asked it things on the shared one (colour scheme...) without saying who we
     are, so GNOME would refuse the shortcut ("invalid app_id"). */
  gchar *addr= g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  GDBusConnection *bus= addr? g_dbus_connection_new_for_address_sync(
                                  addr, GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|
                                                             G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                                  nullptr, nullptr, nullptr):
                            nullptr;
  g_free(addr);
  if(!bus) {
    return wxString();
  }
  /* Tell the portal who we are before anything else (no Flatpak, and no app scope when started
     from a terminal); harmless if this portal is too old to have the registry. */
  GVariant *r= g_dbus_connection_call_sync(bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                           "org.freedesktop.host.portal.Registry", "Register",
                                           g_variant_new("(sa{sv})", app_id, nullptr), nullptr,
                                           G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
  if(r) {
    g_variant_unref(r);
  }
  wxString text;
  if(PortalHotKey::available(bus)) {
    auto *h= new PortalHotKey(f);

    hotkey.reset(h);
    h->start(bus);
    text= "asking the desktop..."; /* set_hotkey_text() follows */
  } else {
    g_dbus_connection_close_sync(bus, nullptr, nullptr);
  }
  g_object_unref(bus);
  return text;
}

/* ------------------------------------------------------------------ .wgq association */

static wxString desktop_file() { return wxString(app_id)+".desktop"; }
static wxString user_desktop_path() { return data_home()+"/applications/"+desktop_file(); }

static wxString query_default()
{
  wxArrayString out, err;

  if(wxExecute(wxString("xdg-mime query default ")+queue_mime, out, err, wxEXEC_SYNC|wxEXEC_NODISABLE)!=0 ||
     out.empty()) {
    return wxString();
  }
  return out[0].Trim().Trim(false);
}

bool platform_assoc_supported()
{
  return in_path("xdg-mime");
}

bool platform_assoc_get()
{
  return platform_assoc_supported() && query_default()==desktop_file();
}

/* Desktop Entry Exec quoting: "..." with \ " ` $ escaped, then \ doubled again for the file */
static wxString exec_quote(const wxString &path)
{
  wxString q= "\"";

  for(wxUniChar c : path) {
    if(c=='"' || c=='`' || c=='$' || c=='\\') {
      q+= "\\";
    }
    q+= c;
  }
  q+= "\"";
  q.Replace("\\", "\\\\");
  return q;
}

static bool write_user_files()
{
  bool ok= true;
  wxString dh= data_home();

  /* The MIME type, unless a package installed it */
  if(!system_data_file(wxString("mime/packages/")+app_id+".xml")) {
    ok&= write_file(dh+"/mime/packages/"+app_id+".xml",
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                    "  <mime-type type=\"application/x-wackget-queue\">\n"
                    "    <comment>WackGet queue</comment>\n"
                    "    <icon name=\"io.github.levoniust.WackGet\"/>\n"
                    "    <glob pattern=\"*.wgq\"/>\n"
                    "  </mime-type>\n"
                    "</mime-info>\n");
    if(in_path("update-mime-database")) {
      wxArrayString out, err;
      wxExecute("update-mime-database \""+dh+"/mime\"", out, err, wxEXEC_SYNC|wxEXEC_NODISABLE);
    }
  }
  /* A launcher for this copy of WackGet, unless a package installed one */
  if(!system_data_file("applications/"+desktop_file())) {
    wxString exe= wxStandardPaths::Get().GetExecutablePath();

    ok&= write_file(user_desktop_path(),
                    "[Desktop Entry]\n"
                    "Type=Application\n"
                    "Name=WackGet\n"
                    "Comment=Download manager: paste or drop links and let it fetch them\n"
                    "Exec="+exec_quote(exe)+" %F\n"
                    "Icon=io.github.levoniust.WackGet\n"
                    "Categories=Network;FileTransfer;\n"
                    "MimeType=application/x-wackget-queue;\n"
                    "StartupWMClass=WackGet\n"
                    "X-WackGet-Written-By-Preferences=true\n");
    wxIconBundle icons= wackget_icons();
    for(int size : {16, 32, 48}) {
      wxImage img= icon_image(icons.GetIcon(wxSize(size, size), wxIconBundle::FALLBACK_NEAREST_LARGER), size);
      wxString p= wxString::Format("%s/icons/hicolor/%dx%d/apps/%s.png", dh, size, size, app_id);

      wxFileName::Mkdir(wxFileName(p).GetPath(), 0755, wxPATH_MKDIR_FULL);
      ok&= img.IsOk() && img.SaveFile(p, wxBITMAP_TYPE_PNG);
    }
    if(in_path("update-desktop-database")) {
      wxArrayString out, err;
      wxExecute("update-desktop-database \""+dh+"/applications\"", out, err, wxEXEC_SYNC|wxEXEC_NODISABLE);
    }
  }
  return ok;
}

/* Take our default out of ~/.config/mimeapps.list, leaving the rest of the file alone */
static void remove_default()
{
  wxString path= config_home()+"/mimeapps.list";
  if(!wxFileExists(path)) {
    return; /* (wx would show an error box for a missing file) */
  }
  wxFileInputStream in(path);
  if(!in.IsOk()) {
    return;
  }
  wxString text, section;
  wxStringOutputStream s(&text);
  in.Read(s);

  wxString out;
  for(const wxString &line : wxSplit(text, '\n', '\0')) {
    wxString t= line;
    t.Trim().Trim(false);
    if(t.StartsWith("[")) {
      section= t;
    }
    if(section=="[Default Applications]" && t.StartsWith(wxString(queue_mime)+"=")) {
      continue;
    }
    out+= line+"\n";
  }
  if(out.EndsWith("\n\n") && !text.EndsWith("\n\n")) {
    out.RemoveLast();
  }
  write_file(path, out);
}

bool platform_assoc_set(bool on)
{
  if(on) {
    bool ok= write_user_files();
    wxArrayString out, err;

    wxFileName::Mkdir(config_home(), 0700, wxPATH_MKDIR_FULL); /* xdg-mime won't create it */
    ok&= wxExecute("xdg-mime default "+desktop_file()+" "+queue_mime, out, err, wxEXEC_SYNC|wxEXEC_NODISABLE)==0;
    return ok;
  }
  if(platform_assoc_get()) { /* only undo our own association */
    remove_default();
    wxString p= user_desktop_path(), text;
    if(wxFileExists(p)) { /* not there when a package installed the launcher */
      wxFileInputStream in(p);
      wxStringOutputStream s(&text);
      in.Read(s);
    }
    if(text.Contains("X-WackGet-Written-By-Preferences=true")) {
      wxRemoveFile(p);
      if(in_path("update-desktop-database")) {
        wxArrayString out, err;
        wxExecute("update-desktop-database \""+data_home()+"/applications\"", out, err, wxEXEC_SYNC|wxEXEC_NODISABLE);
      }
    }
  }
  return true;
}
