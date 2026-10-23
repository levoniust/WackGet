/*
 * Skinned queue list (Windows 11 and Material Design 1): a header and a row area drawn with the
 * skin's colours. Behaves like the Classic list view: multi-select with Ctrl/Shift, keyboard
 * navigation, resizable and draggable columns, label tips, drag-to-reorder rows.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#include <algorithm>
#include <memory>

#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include "prefs.h"
#include "views.h"

namespace {

class SkinView;

/* Paints with antialiasing (rounded selection, progress bars). */
struct SmoothDC {
  explicit SmoothDC(wxWindow *w): pdc(w)
  {
    wxGraphicsContext *gc= wxGraphicsContext::CreateFromUnknownDC(pdc);

    if(gc) {
      gcdc.reset(new wxGCDC(gc));
    }
  }
  wxDC &dc() { return gcdc? (wxDC &)*gcdc: (wxDC &)pdc; }
  wxAutoBufferedPaintDC pdc;
  std::unique_ptr<wxGCDC> gcdc;
};

class Header : public wxWindow {
public:
  Header(wxWindow *parent, SkinView *v);
  void on_paint(wxPaintEvent &);
  void on_mouse(wxMouseEvent &);
  int scroll_x= 0;

private:
  int column_at(int x, int *edge) const;
  SkinView *v_;
  int resizing_= -1, resize_start_x_= 0, resize_start_w_= 0;
  int pressed_= -1, press_x_= 0;
  bool reordering_= false;
};

class Rows : public wxScrolled<wxWindow> {
public:
  Rows(wxWindow *parent, SkinView *v);
  void on_paint(wxPaintEvent &);
  void on_mouse(wxMouseEvent &);
  void on_key(wxKeyEvent &);
  int row_at(int client_y) const;
  void update_virtual_size();

private:
  void select_only(int row);
  void select_range(int from, int to);
  void move_focus(int row, bool extend);
  void update_tip(const wxPoint &client);
  SkinView *v_;
  int anchor_= -1, hover_= -1;
  int drag_row_= -1;
  bool maybe_drag_= false;
  wxPoint down_pos_;
  wxString tip_;
};

class SkinView : public QueueView {
public:
  SkinView(wxWindow *parent, Queue &queue, ViewOwner *own, const Skin &sk): q(queue), owner(own), skin(sk)
  {
    panel= new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    panel->SetBackgroundColour(sk.bg);
    header= new Header(panel, this);
    rows= new Rows(panel, this);
    auto *sz= new wxBoxSizer(wxVERTICAL);
    sz->Add(header, 0, wxEXPAND);
    sz->Add(rows, 1, wxEXPAND);
    panel->SetSizer(sz);
    load_columns();
    grid= prefs::get_int("ShowGridLines", 1)!=0;
    set_show_header(prefs::get_int("ShowColumnHeaders", 1)!=0);
  }

  wxWindow *window() override { return panel; }
  wxWindow *drop_window() override { return rows; }

  void refresh() override
  {
    if(focus>=(int)q.size()) {
      focus= (int)q.size()-1;
    }
    rows->update_virtual_size();
    rows->Refresh(false);
  }

  void sync_selection_from_items() override { rows->Refresh(false); }
  void sync_selection_to_items() override {} /* the rows draw straight from Item::selected */

  void ensure_visible(int row) override
  {
    if(row<0 || (size_t)row>=q.size()) {
      return;
    }
    int first, unused;

    rows->GetViewStart(&unused, &first);
    int visible= std::max(1, rows->GetClientSize().y/skin.row_h);
    if(row<first) {
      rows->Scroll(-1, row);
    } else if(row>=first+visible) {
      rows->Scroll(-1, row-visible+1);
    }
  }

  void set_focus_row(int row) override { focus= row; }

  void set_show_header(bool on) override
  {
    header->Show(on);
    panel->Layout();
  }

  void set_grid_lines(bool on) override
  {
    grid= on;
    rows->Refresh(false);
  }

  void load_columns() override
  {
    for(int i= 0; i<COL_COUNT; i++) {
      char key[32];

      snprintf(key, sizeof(key), "LastColumn%dWidth", i);
      /* The skins have more padding than the classic list: never narrower than the default. */
      width[i]= std::max(prefs::get_int(key, 0), panel->FromDIP(default_column_width[i]));
    }
    order= load_column_order();
  }

  void save_columns() override
  {
    for(int i= 0; i<COL_COUNT; i++) {
      char key[32];

      snprintf(key, sizeof(key), "LastColumn%dWidth", i);
      prefs::set_int(key, width[i]);
    }
    save_column_order(order);
  }

  int total_width() const
  {
    int w= 0;

    for(int i= 0; i<COL_COUNT; i++) {
      w+= width[i];
    }
    return w;
  }

  Queue &q;
  ViewOwner *owner;
  Skin skin;
  wxWindow *panel;
  Header *header;
  Rows *rows;
  int width[COL_COUNT]= {};
  std::vector<int> order;
  int focus= -1;
  bool grid= true;
};

/* ------------------------------------------------------------------ header */

Header::Header(wxWindow *parent, SkinView *v): wxWindow(parent, wxID_ANY), v_(v)
{
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(-1, v->skin.header_h));
  Bind(wxEVT_PAINT, &Header::on_paint, this);
  Bind(wxEVT_LEFT_DOWN, &Header::on_mouse, this);
  Bind(wxEVT_LEFT_UP, &Header::on_mouse, this);
  Bind(wxEVT_MOTION, &Header::on_mouse, this);
  Bind(wxEVT_LEAVE_WINDOW, &Header::on_mouse, this);
  Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { resizing_= pressed_= -1; });
}

/* Column (in display order) under x; *edge = 1 when x is on its right border (resize). */
int Header::column_at(int x, int *edge) const
{
  int cx= -scroll_x, grip= FromDIP(4);

  *edge= 0;
  for(size_t i= 0; i<v_->order.size(); i++) {
    int w= v_->width[v_->order[i]];

    if(x>=cx+w-grip && x<=cx+w+grip) {
      *edge= 1;
      return (int)i;
    }
    if(x>=cx && x<cx+w) {
      return (int)i;
    }
    cx+= w;
  }
  return -1;
}

void Header::on_paint(wxPaintEvent &)
{
  SmoothDC sdc(this);
  wxDC &dc= sdc.dc();
  const Skin &s= v_->skin;
  wxSize sz= GetClientSize();
  int x= -scroll_x;

  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(s.header_bg));
  dc.DrawRectangle(0, 0, sz.x, sz.y);
  dc.SetFont(s.header_font);
  dc.SetTextForeground(s.header_text);
  for(size_t i= 0; i<v_->order.size(); i++) {
    int col= v_->order[i], w= v_->width[col];
    wxString label= wxControl::Ellipsize(column_names[col], dc, wxELLIPSIZE_END, std::max(0, w-2*s.pad));
    wxSize te= dc.GetTextExtent(label);

    dc.DrawText(label, x+s.pad, (sz.y-te.y)/2);
    x+= w;
    if(s.id==SkinId::Win11) { /* Fluent: short divider between column titles */
      dc.SetPen(wxPen(s.header_line, 1));
      dc.DrawLine(x-1, sz.y/4, x-1, sz.y-sz.y/4);
      dc.SetPen(*wxTRANSPARENT_PEN);
    }
  }
  if(s.id==SkinId::Win11) {
    dc.SetPen(wxPen(s.header_line, 1));
    dc.DrawLine(0, sz.y-1, sz.x, sz.y-1);
  }
}

void Header::on_mouse(wxMouseEvent &e)
{
  int edge= 0, col= column_at(e.GetX(), &edge);

  if(e.LeftDown()) {
    if(col>=0 && edge) {
      resizing_= v_->order[(size_t)col];
      resize_start_x_= e.GetX();
      resize_start_w_= v_->width[resizing_];
    } else {
      pressed_= col;
      press_x_= e.GetX();
      reordering_= false;
    }
    if(!HasCapture()) {
      CaptureMouse();
    }
    return;
  }
  if(e.LeftUp()) {
    if(HasCapture()) {
      ReleaseMouse();
    }
    if(reordering_ && pressed_>=0) { /* drop the dragged column where the mouse is */
      int cx= -scroll_x, target= 0;

      for(size_t i= 0; i<v_->order.size(); i++) {
        int w= v_->width[v_->order[i]];

        if(e.GetX()>cx+w/2) {
          target= (int)i+1;
        }
        cx+= w;
      }
      int moved= v_->order[(size_t)pressed_];
      v_->order.erase(v_->order.begin()+pressed_);
      if(target>pressed_) {
        target--;
      }
      v_->order.insert(v_->order.begin()+std::min(target, (int)v_->order.size()), moved);
      Refresh();
      v_->rows->Refresh(false);
    }
    resizing_= pressed_= -1;
    reordering_= false;
    SetCursor(wxNullCursor);
    return;
  }
  if(e.Dragging() && resizing_>=0) {
    v_->width[resizing_]= std::max(FromDIP(30), resize_start_w_+e.GetX()-resize_start_x_);
    Refresh();
    v_->rows->update_virtual_size();
    v_->rows->Refresh(false);
    return;
  }
  if(e.Dragging() && pressed_>=0 && std::abs(e.GetX()-press_x_)>FromDIP(6)) {
    reordering_= true;
    SetCursor(wxCursor(wxCURSOR_SIZING));
    return;
  }
  if(e.Moving() || e.Leaving()) {
    SetCursor((!e.Leaving() && col>=0 && edge)? wxCursor(wxCURSOR_SIZEWE): wxNullCursor);
  }
}

/* ------------------------------------------------------------------ rows */

Rows::Rows(wxWindow *parent, SkinView *v):
  wxScrolled<wxWindow>(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                       wxVSCROLL|wxHSCROLL|wxWANTS_CHARS|wxBORDER_NONE), v_(v)
{
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetScrollRate(FromDIP(16), v->skin.row_h);
  Bind(wxEVT_PAINT, &Rows::on_paint, this);
  Bind(wxEVT_LEFT_DOWN, &Rows::on_mouse, this);
  Bind(wxEVT_LEFT_DCLICK, &Rows::on_mouse, this);
  Bind(wxEVT_LEFT_UP, &Rows::on_mouse, this);
  Bind(wxEVT_RIGHT_DOWN, &Rows::on_mouse, this);
  Bind(wxEVT_MOTION, &Rows::on_mouse, this);
  Bind(wxEVT_LEAVE_WINDOW, &Rows::on_mouse, this);
  Bind(wxEVT_KEY_DOWN, &Rows::on_key, this);
  Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) { Refresh(false); e.Skip(); });
  Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) { Refresh(false); e.Skip(); });
  Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { drag_row_= -1; maybe_drag_= false; });
  Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent &e) {
    if(e.GetPosition()==wxDefaultPosition) { /* keyboard: at the focused row */
      int y= (v_->focus>=0? v_->focus+1: 0)*v_->skin.row_h;
      wxPoint p= CalcScrolledPosition(wxPoint(v_->skin.pad, y));
      v_->owner->on_view_context_menu(v_->focus>=0 && v_->q[(size_t)v_->focus].selected, ClientToScreen(p));
    }
  });
  Bind(wxEVT_SIZE, [this](wxSizeEvent &e) { Refresh(false); e.Skip(); });
}

void Rows::update_virtual_size()
{
  SetVirtualSize(v_->total_width(), (int)v_->q.size()*v_->skin.row_h);
}

int Rows::row_at(int client_y) const
{
  int y= CalcUnscrolledPosition(wxPoint(0, client_y)).y;
  int r= y/v_->skin.row_h;

  return (y>=0 && r<(int)v_->q.size())? r: -1;
}

void Rows::on_paint(wxPaintEvent &)
{
  SmoothDC sdc(this);
  wxDC &dc= sdc.dc();
  const Skin &s= v_->skin;
  Queue &q= v_->q;
  wxSize cs= GetClientSize();
  wxPoint origin= CalcUnscrolledPosition(wxPoint(0, 0));
  int full_w= std::max(v_->total_width(), cs.x+origin.x);

  if(v_->header->scroll_x!=origin.x) { /* keep the header lined up with horizontal scrolling */
    v_->header->scroll_x= origin.x;
    v_->header->Refresh();
  }
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.SetBrush(wxBrush(s.bg));
  dc.DrawRectangle(0, 0, cs.x, cs.y);
  dc.SetDeviceOrigin(-origin.x, -origin.y);

  int first= std::max(0, origin.y/s.row_h);
  int last= std::min((int)q.size()-1, (origin.y+cs.y)/s.row_h);
  bool focused= HasFocus();

  for(int r= first; r<=last; r++) {
    const Item &it= q[(size_t)r];
    int y= r*s.row_h;
    wxColour fill;

    if(it.selected) {
      fill= s.select;
    } else if(r==hover_) {
      fill= s.hover;
    }
    if(fill.IsOk()) {
      dc.SetBrush(wxBrush(fill));
      dc.SetPen(*wxTRANSPARENT_PEN);
      if(s.inset_select) {
        int in= FromDIP(4);
        dc.DrawRoundedRectangle(origin.x+in, y+FromDIP(2), cs.x-2*in, s.row_h-FromDIP(4), s.radius);
      } else {
        dc.DrawRectangle(0, y, full_w, s.row_h);
      }
    }
    if(it.selected && s.pill) { /* Fluent accent pill */
      int ph= s.row_h*2/5, in= FromDIP(4);
      dc.SetBrush(wxBrush(s.accent));
      dc.DrawRoundedRectangle(origin.x+in, y+(s.row_h-ph)/2, FromDIP(3), ph, FromDIP(3)/2.0);
    }
    if(focused && r==v_->focus) { /* keyboard focus */
      dc.SetBrush(*wxTRANSPARENT_BRUSH);
      dc.SetPen(wxPen(s.id==SkinId::Win11? s.text2: s.header_bg, 1));
      int in= s.inset_select? FromDIP(4): 1;
      dc.DrawRoundedRectangle(origin.x+in, y+(s.inset_select? FromDIP(2): 1),
                              cs.x-2*in, s.row_h-(s.inset_select? FromDIP(4): 2), s.radius);
      dc.SetPen(*wxTRANSPARENT_PEN);
    }
    if(v_->grid && s.divider.IsOk()) {
      dc.SetPen(wxPen(s.divider, 1));
      dc.DrawLine(0, y+s.row_h-1, full_w, y+s.row_h-1);
      dc.SetPen(*wxTRANSPARENT_PEN);
    } else if(v_->grid && s.id==SkinId::Win11 && !it.selected && r!=hover_ && r+1<(int)q.size()) {
      dc.SetPen(wxPen(s.header_line, 1));
      dc.DrawLine(origin.x+FromDIP(12), y+s.row_h-1, origin.x+cs.x-FromDIP(12), y+s.row_h-1);
      dc.SetPen(*wxTRANSPARENT_PEN);
    }

    int x= 0;
    for(int col : v_->order) {
      int w= v_->width[col];
      wxColour fg= (col==COL_NAME)? (it.selected? s.select_text: s.text): s.text2;

      if(col==COL_STATUS && s.status_colors) {
        if(it.status==WG_STAT_DONE) {
          fg= s.ok;
        } else if(it.status==WG_STAT_FAILED) {
          fg= s.bad;
        } else if(it.status==WG_STAT_GOING) {
          fg= (s.id==SkinId::Material1)? (s.dark? s.text: s.header_bg): s.accent;
        }
      }
      dc.SetFont(s.font);
      dc.SetTextForeground(fg);
      wxString t= wxControl::Ellipsize(wxString::FromUTF8(it.text[col]), dc, wxELLIPSIZE_END,
                                       std::max(0, w-2*s.pad));
      wxSize te= dc.GetTextExtent(t.empty()? wxString("Ag"): t);
      bool bar= col==COL_PROGRESS && (it.fraction>=0 || it.status==WG_STAT_GOING);
      int ty= y+(s.row_h-te.y)/2-(bar? s.row_h/8: 0);
      dc.DrawText(t, x+s.pad, ty);

      if(bar) {
        int bx= x+s.pad, bw= std::max(0, w-2*s.pad), bh= s.bar_thickness;
        int by= ty+te.y+FromDIP(3);
        double frac= std::min(1.0, std::max(0.0, it.fraction));
        wxColour c= s.accent;

        if(it.status==WG_STAT_DONE) {
          c= s.ok;
          frac= 1.0;
        } else if(it.status==WG_STAT_FAILED) {
          c= s.bad;
        } else if(it.status==WG_STAT_PAUSED || it.status==WG_STAT_QUEUED) {
          c= (s.id==SkinId::Win11)? (s.dark? wxColour(0xFC, 0xE1, 0x00): wxColour(0x9D, 0x5D, 0x00)): s.text2;
        }
        double rad= (s.id==SkinId::Win11)? bh/2.0: 0;
        dc.SetBrush(wxBrush(s.track));
        if(s.id==SkinId::Win11) { /* Fluent: 1px track line, 3px bar */
          dc.DrawRoundedRectangle(bx, by+bh/2, bw, 1, 0);
        } else {
          dc.DrawRectangle(bx, by, bw, bh);
        }
        if(it.fraction<0) { /* size unknown: a short segment, like an indeterminate bar */
          frac= 0.25;
        }
        dc.SetBrush(wxBrush(c));
        if(frac>0) {
          dc.DrawRoundedRectangle(bx, by, std::max(bh, (int)(bw*frac)), bh, rad);
        }
      }
      x+= w;
    }
  }

  dc.SetDeviceOrigin(0, 0);
  if(q.size()==0) { /* empty queue: say how to fill it */
    wxString hint= "Paste links (Ctrl+V) or drop them here";
    dc.SetFont(s.font);
    dc.SetTextForeground(s.text2);
    wxSize te= dc.GetTextExtent(hint);
    dc.DrawText(hint, (cs.x-te.x)/2, std::max(FromDIP(12), (cs.y-te.y)/2));
  }
  if(s.header_shadow && v_->header->IsShown()) { /* Material: elevation shadow under the app bar */
    int sh= FromDIP(6);
    for(int i= 0; i<sh; i++) {
      int a= (sh-i)*(s.dark? 14: 9);
      dc.SetPen(wxPen(wxColour(0, 0, 0, (unsigned char)std::min(a, 255)), 1));
      dc.DrawLine(0, i, cs.x, i);
    }
  }
}

void Rows::select_only(int row)
{
  for(size_t i= 0; i<v_->q.size(); i++) {
    v_->q[i].selected= (int)i==row;
  }
}

void Rows::select_range(int from, int to)
{
  if(from>to) {
    std::swap(from, to);
  }
  for(size_t i= 0; i<v_->q.size(); i++) {
    v_->q[i].selected= (int)i>=from && (int)i<=to;
  }
}

void Rows::move_focus(int row, bool extend)
{
  int n= (int)v_->q.size();

  if(n==0) {
    return;
  }
  row= std::max(0, std::min(n-1, row));
  if(extend && anchor_>=0) {
    select_range(anchor_, row);
  } else {
    select_only(row);
    anchor_= row;
  }
  v_->focus= row;
  v_->ensure_visible(row);
  Refresh(false);
}

void Rows::on_key(wxKeyEvent &e)
{
  int page= std::max(1, GetClientSize().y/v_->skin.row_h-1);
  bool shift= e.ShiftDown();
  int f= v_->focus;

  if(e.AltDown()) { /* Alt+Up/Down are the Move Up/Down accelerators */
    e.Skip();
    return;
  }
  switch(e.GetKeyCode()) {
  case WXK_UP:
    move_focus(f<0? 0: f-1, shift);
    break;
  case WXK_DOWN:
    move_focus(f+1, shift);
    break;
  case WXK_PAGEUP:
    move_focus(f-page, shift);
    break;
  case WXK_PAGEDOWN:
    move_focus(f+page, shift);
    break;
  case WXK_HOME:
    move_focus(0, shift);
    break;
  case WXK_END:
    move_focus((int)v_->q.size()-1, shift);
    break;
  case WXK_SPACE:
    if(e.ControlDown() && f>=0 && (size_t)f<v_->q.size()) {
      v_->q[(size_t)f].selected= !v_->q[(size_t)f].selected;
      anchor_= f;
      Refresh(false);
    } else {
      e.Skip();
    }
    break;
  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    if(!e.ControlDown()) {
      v_->owner->on_view_activate();
    } else {
      e.Skip();
    }
    break;
  default:
    e.Skip();
  }
}

void Rows::update_tip(const wxPoint &client)
{
  int r= row_at(client.y);
  wxString tip;

  if(r>=0) {
    int x= CalcUnscrolledPosition(client).x, cx= 0;
    const Item &it= v_->q[(size_t)r];

    for(int col : v_->order) {
      int w= v_->width[col];

      if(x>=cx && x<cx+w) {
        wxString full= wxString::FromUTF8(it.text[col]);
        wxClientDC cdc(this);
        cdc.SetFont(v_->skin.font);
        if(col==COL_STATUS && it.status==WG_STAT_FAILED && !it.message.empty()) {
          tip= wxString::FromUTF8(it.message);
        } else if(cdc.GetTextExtent(full).x>w-2*v_->skin.pad) { /* label tip for cut-off text */
          tip= full;
        }
        break;
      }
      cx+= w;
    }
  }
  if(tip!=tip_) {
    tip_= tip;
    if(tip.empty()) {
      UnsetToolTip();
    } else {
      SetToolTip(tip);
    }
  }
}

void Rows::on_mouse(wxMouseEvent &e)
{
  int r= row_at(e.GetY());
  Queue &q= v_->q;

  if(e.Leaving()) {
    if(hover_!=-1) {
      hover_= -1;
      Refresh(false);
    }
    return;
  }
  if(e.Moving()) {
    if(r!=hover_) {
      hover_= r;
      Refresh(false);
    }
    update_tip(e.GetPosition());
    return;
  }
  if(e.LeftDown() || e.RightDown()) {
    SetFocus();
    if(r<0) {
      if(e.LeftDown() && !e.ControlDown() && !e.ShiftDown()) {
        select_only(-1);
      }
    } else if(e.ShiftDown() && anchor_>=0) {
      select_range(anchor_, r);
    } else if(e.ControlDown() && e.LeftDown()) {
      q[(size_t)r].selected= !q[(size_t)r].selected;
      anchor_= r;
    } else if(!q[(size_t)r].selected || e.LeftDown()) {
      if(!q[(size_t)r].selected) {
        select_only(r);
      }
      anchor_= r;
    }
    if(r>=0) {
      v_->focus= r;
    }
    Refresh(false);
    if(e.RightDown()) {
      v_->owner->on_view_context_menu(r>=0, ClientToScreen(e.GetPosition()));
      return;
    }
    maybe_drag_= r>=0 && !e.ControlDown() && !e.ShiftDown();
    drag_row_= -1;
    down_pos_= e.GetPosition();
    if(!HasCapture()) {
      CaptureMouse();
    }
    return;
  }
  if(e.LeftDClick()) {
    if(r>=0) {
      select_only(r);
      v_->focus= r;
      Refresh(false);
      v_->owner->on_view_activate();
    }
    return;
  }
  if(e.LeftUp()) {
    if(HasCapture()) {
      ReleaseMouse();
    }
    /* A plain click on a row of a multi-selection selects just that row (when it wasn't a drag) */
    if(maybe_drag_ && drag_row_<0 && r>=0 && !e.ControlDown() && !e.ShiftDown()) {
      select_only(r);
      Refresh(false);
    }
    maybe_drag_= false;
    drag_row_= -1;
    return;
  }
  if(e.Dragging() && maybe_drag_) {
    if(drag_row_<0) {
      if(std::abs(e.GetY()-down_pos_.y)<FromDIP(4)) {
        return;
      }
      drag_row_= row_at(down_pos_.y);
    }
    /* Clamp to the list so dragging past the top or bottom still moves the rows there. */
    int over= r;
    if(over<0) {
      over= (e.GetY()<0)? 0: (int)q.size()-1;
    }
    if(drag_row_>=0 && over>=0 && over!=drag_row_) {
      drag_row_= v_->owner->on_view_drag(drag_row_, over);
      v_->focus= drag_row_;
      anchor_= drag_row_;
    }
  }
}

} // namespace

QueueView *create_skin_view(wxWindow *parent, Queue &q, ViewOwner *owner, const Skin &skin)
{
  return new SkinView(parent, q, owner, skin);
}
