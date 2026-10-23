/*
 * Skins. "Classic" is the original: native list view and status bar. The others draw the queue
 * themselves: "Windows 11" (Fluent: Segoe UI, subtle fills, accent pill, light/dark from the system)
 * and "Material Design 1" (2014 Material: Roboto, indigo app-bar header, pink accent, 48dp rows).
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#ifndef WACKGET_UI_SKIN_H
#define WACKGET_UI_SKIN_H

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>

class wxWindow;

enum class SkinId { Classic, Win11, Material1, Count };

struct Skin {
  SkinId id= SkinId::Classic;
  bool dark= false;

  wxColour bg;          /* rows background */
  wxColour text, text2; /* primary / secondary text */
  wxColour header_bg, header_text, header_line;
  wxColour divider;     /* between rows (empty = none) */
  wxColour hover, select, select_text;
  wxColour accent;      /* selection pill, progress bar */
  wxColour track;       /* progress bar background */
  wxColour ok, bad;     /* Complete / Failed */
  wxColour bar_bg, bar_text, bar_line; /* status bar */
  wxFont font, header_font, bar_font;

  int row_h= 0, header_h= 0, bar_h= 0; /* pixels (already scaled for DPI) */
  int pad= 0;           /* cell padding */
  int radius= 0;        /* selection corner radius */
  int bar_thickness= 0; /* progress bar */
  bool pill= false;     /* Fluent accent pill on the selected row */
  bool inset_select= false;
  bool header_shadow= false; /* Material elevation under the app-bar header */
  bool status_colors= false; /* Complete/Failed in colour */
};

SkinId skin_from_name(const wxString &name);
const char *skin_name(SkinId id);  /* stored in the settings: classic, win11, material1 */
wxString skin_label(SkinId id);    /* shown in Preferences */
/* Builds the colours/fonts/sizes for a window (for its DPI and the system's light/dark mode). */
Skin make_skin(SkinId id, wxWindow *win);

#endif
