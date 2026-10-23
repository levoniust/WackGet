/*
 * Classic skin: the original report-mode list view (list.c create_list()), virtual so the queue
 * is the only copy of the data.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#include <wx/listctrl.h>
#include <wx/timer.h>
#include <wx/utils.h>

#ifdef __WXMSW__
#include <commctrl.h>
#endif

#include "prefs.h"
#include "views.h"

namespace {

class ListCtrl : public wxListCtrl {
public:
  ListCtrl(wxWindow *parent, Queue &q, long style): wxListCtrl(parent, wxID_ANY, wxDefaultPosition,
                                                               wxDefaultSize, style), q_(q) {}
  wxString OnGetItemText(long item, long column) const override
  {
    if(item<0 || (size_t)item>=q_.size() || column<0 || column>=COL_COUNT) {
      return wxString();
    }
    return wxString::FromUTF8(q_[(size_t)item].text[column]);
  }

private:
  Queue &q_;
};

class NativeView : public QueueView {
public:
  NativeView(wxWindow *parent, Queue &q, ViewOwner *owner): q_(q), owner_(owner)
  {
    long style= wxLC_REPORT|wxLC_VIRTUAL|wxBORDER_THEME;

    if(!prefs::get_int("ShowColumnHeaders", 1)) {
      style|= wxLC_NO_HEADER;
    }
    if(prefs::get_int("ShowGridLines", 1)) {
      style|= wxLC_HRULES|wxLC_VRULES;
    }
    list_= new ListCtrl(parent, q, style);
    for(int i= 0; i<COL_COUNT; i++) {
      list_->AppendColumn(column_names[i]);
    }
#ifdef __WXMSW__
    /* LVS_EX_LABELTIP|LVS_EX_HEADERDRAGDROP like the original (full-row select is wx's default) */
    ListView_SetExtendedListViewStyleEx((HWND)list_->GetHWND(), LVS_EX_LABELTIP|LVS_EX_HEADERDRAGDROP,
                                        LVS_EX_LABELTIP|LVS_EX_HEADERDRAGDROP);
#endif
    load_columns();
    list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent &) { owner_->on_view_activate(); });
    list_->Bind(wxEVT_LIST_BEGIN_DRAG, &NativeView::on_begin_drag, this);
    list_->Bind(wxEVT_CONTEXT_MENU, &NativeView::on_context_menu, this);
    drag_timer_.Bind(wxEVT_TIMER, &NativeView::on_drag_timer, this);
  }

  wxWindow *window() override { return list_; }

  void refresh() override
  {
    if((size_t)list_->GetItemCount()!=q_.size()) {
      list_->SetItemCount((long)q_.size());
    }
    if(q_.size()>0) {
      list_->RefreshItems(0, (long)q_.size()-1);
    }
  }

  void sync_selection_from_items() override
  {
    for(size_t i= 0; i<q_.size(); i++) {
      bool want= q_[i].selected;

      if((list_->GetItemState((long)i, wxLIST_STATE_SELECTED)!=0)!=want) {
        list_->SetItemState((long)i, want? wxLIST_STATE_SELECTED: 0, wxLIST_STATE_SELECTED);
      }
    }
  }

  void sync_selection_to_items() override
  {
    for(size_t i= 0; i<q_.size(); i++) {
      q_[i].selected= (list_->GetItemState((long)i, wxLIST_STATE_SELECTED)!=0);
    }
  }

  void ensure_visible(int row) override
  {
    if(row>=0 && (size_t)row<q_.size()) {
      list_->EnsureVisible(row);
    }
  }

  void set_focus_row(int row) override
  {
    if(row>=0 && (size_t)row<q_.size()) {
      list_->SetItemState(row, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
    }
  }

  void set_show_header(bool on) override
  {
    list_->SetSingleStyle(wxLC_NO_HEADER, !on);
  }

  void set_grid_lines(bool on) override
  {
    list_->SetSingleStyle(wxLC_HRULES, on);
    list_->SetSingleStyle(wxLC_VRULES, on);
    list_->Refresh();
  }

  void load_columns() override
  {
    for(int i= 0; i<COL_COUNT; i++) {
      char key[32];

      snprintf(key, sizeof(key), "LastColumn%dWidth", i);
      list_->SetColumnWidth(i, prefs::get_int(key, list_->FromDIP(default_column_width[i])));
    }
#ifdef wxHAS_LISTCTRL_COLUMN_ORDER
    std::vector<int> order= load_column_order();
    wxArrayInt a;

    for(int o : order) {
      a.push_back(o);
    }
    list_->SetColumnsOrder(a);
#endif
  }

  void save_columns() override
  {
    for(int i= 0; i<COL_COUNT; i++) {
      char key[32];

      snprintf(key, sizeof(key), "LastColumn%dWidth", i);
      prefs::set_int(key, list_->GetColumnWidth(i));
    }
#ifdef wxHAS_LISTCTRL_COLUMN_ORDER
    wxArrayInt a= list_->GetColumnsOrder();
    std::vector<int> order(a.begin(), a.end());

    save_column_order(order);
#endif
  }

private:
  void on_context_menu(wxContextMenuEvent &e)
  {
    wxPoint pos= e.GetPosition();
    int flags= 0;

    if(pos==wxDefaultPosition) { /* keyboard (Menu key / Shift+F10): at the focused item */
      long f= list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
      wxRect r;

      if(f>=0 && list_->GetItemRect(f, r)) {
        owner_->on_view_context_menu(true, list_->ClientToScreen(r.GetBottomLeft()));
      } else {
        owner_->on_view_context_menu(false, list_->ClientToScreen(wxPoint(0, 0)));
      }
      return;
    }
    long item= list_->HitTest(hit_window()->ScreenToClient(pos), flags);

    owner_->on_view_context_menu(item!=wxNOT_FOUND, pos);
  }

  /* The window whose coordinates HitTest() takes: the list itself on Windows, the generic
     implementation's inner row window elsewhere. */
  wxWindow *hit_window()
  {
#ifdef __WXMSW__
    return list_;
#else
    wxWindow *best= list_;
    int h= 0;

    for(wxWindow *c : list_->GetChildren()) {
      if(c->GetSize().y>h) {
        h= c->GetSize().y;
        best= c;
      }
    }
    return best;
#endif
  }

  void on_begin_drag(wxListEvent &e)
  {
    drag_row_= (int)e.GetIndex();
    drag_timer_.Start(30);
  }

  /* Follows the mouse until the button is let go (original on_mousemove/on_lbuttonup). */
  void on_drag_timer(wxTimerEvent &)
  {
    wxMouseState ms= wxGetMouseState();

    if(!ms.LeftIsDown() || drag_row_<0) {
      drag_timer_.Stop();
      drag_row_= -1;
      return;
    }
    wxWindow *w= hit_window();
    wxPoint pt= w->ScreenToClient(ms.GetPosition());
    int flags= 0;

    pt.x= 2;
    long over= list_->HitTest(pt, flags);
    if(over!=wxNOT_FOUND && over!=drag_row_) {
      sync_selection_to_items();
      drag_row_= owner_->on_view_drag(drag_row_, (int)over);
    }
  }

  Queue &q_;
  ViewOwner *owner_;
  ListCtrl *list_;
  wxTimer drag_timer_;
  int drag_row_= -1;
};

} // namespace

QueueView *create_native_view(wxWindow *parent, Queue &q, ViewOwner *owner)
{
  return new NativeView(parent, q, owner);
}

const int default_column_width[COL_COUNT]= {260, 110, 190, 140};

std::vector<int> load_column_order()
{
  std::string s= prefs::get_str("LastColumnOrder", "");
  std::vector<int> order;
  bool seen[COL_COUNT]= {};

  for(size_t i= 0; i<s.size(); i++) {
    if(s[i]>='0' && s[i]<'0'+COL_COUNT && !seen[s[i]-'0']) {
      seen[s[i]-'0']= true;
      order.push_back(s[i]-'0');
    }
  }
  if(order.size()!=COL_COUNT) { /* missing or damaged: default order */
    order.clear();
    for(int i= 0; i<COL_COUNT; i++) {
      order.push_back(i);
    }
  }
  return order;
}

void save_column_order(const std::vector<int> &order)
{
  std::string s;

  for(int o : order) {
    if(!s.empty()) {
      s+= ',';
    }
    s+= (char)('0'+o);
  }
  prefs::set_str("LastColumnOrder", s);
}
