/*
 * The queue list. NativeView is the original report-mode list view (Classic skin); SkinView draws
 * the rows itself for the other skins. Both show the same Queue and report to the frame through
 * ViewOwner.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#ifndef WACKGET_UI_VIEWS_H
#define WACKGET_UI_VIEWS_H

#include <vector>

#include <wx/window.h>

#include "queue.h"
#include "skin.h"

class ViewOwner {
public:
  virtual ~ViewOwner() {}
  virtual void on_view_context_menu(bool on_item, const wxPoint &screen_pos)= 0;
  virtual void on_view_activate()= 0; /* Enter / double-click: Run */
  /* A drag of the selection reached row (original on_mousemove); returns the new row of the
     dragged item. */
  virtual int on_view_drag(int drag_row, int over_row)= 0;
};

class QueueView {
public:
  virtual ~QueueView() {}
  virtual wxWindow *window()= 0;
  virtual wxWindow *drop_window() { return window(); } /* gets the drop target */
  /* The queue changed (rows added/removed/moved, texts updated). */
  virtual void refresh()= 0;
  /* Selection is kept in Item::selected so it survives skin changes; views keep it in sync. */
  virtual void sync_selection_from_items()= 0;
  virtual void sync_selection_to_items()= 0;
  virtual void ensure_visible(int row)= 0;
  virtual void set_focus_row(int row)= 0;
  virtual void set_show_header(bool on)= 0;
  virtual void set_grid_lines(bool on)= 0;
  /* Column widths and order, under the original names LastColumn<n>Width / LastColumnOrder. */
  virtual void load_columns()= 0;
  virtual void save_columns()= 0;
};

QueueView *create_native_view(wxWindow *parent, Queue &q, ViewOwner *owner);
QueueView *create_skin_view(wxWindow *parent, Queue &q, ViewOwner *owner, const Skin &skin);

/* Default column widths (DIP) and the saved order "0,1,2,3". */
extern const int default_column_width[COL_COUNT];
std::vector<int> load_column_order();
void save_column_order(const std::vector<int> &order);

#endif
