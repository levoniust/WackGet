/*
 * Skin palettes. Windows 11 values follow the Fluent / WinUI 3 light and dark theme resources;
 * Material values follow the 2014 Material Design spec (Indigo 500 primary, Pink A200 accent,
 * data-table metrics).
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#include "skin.h"

#include <wx/fontenum.h>
#include <wx/settings.h>
#include <wx/window.h>

#include "prefs.h"

SkinId skin_from_name(const wxString &name)
{
  for(int i= 0; i<(int)SkinId::Count; i++) {
    if(name.CmpNoCase(skin_name((SkinId)i))==0) {
      return (SkinId)i;
    }
  }
  return SkinId::Classic;
}

const char *skin_name(SkinId id)
{
  switch(id) {
  case SkinId::Win11:
    return "win11";
  case SkinId::Material1:
    return "material1";
  default:
    return "classic";
  }
}

wxString skin_label(SkinId id)
{
  switch(id) {
  case SkinId::Win11:
    return "Windows 11";
  case SkinId::Material1:
    return "Material Design 1";
  default:
    return "Classic";
  }
}

static wxString first_face(std::initializer_list<const char *> faces)
{
  for(const char *f : faces) {
    if(wxFontEnumerator::IsValidFacename(f)) {
      return f;
    }
  }
  return wxString();
}

static wxFont make_font(const wxString &face, double points, wxFontWeight weight)
{
  wxFont f= wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);

  if(!face.empty()) {
    f.SetFaceName(face);
  }
  f.SetFractionalPointSize(points);
  f.SetWeight(weight);
  return f;
}

/* "SkinDarkMode": 0 = follow the system, 1 = light, 2 = dark */
static bool want_dark()
{
  switch(prefs::get_int("SkinDarkMode", 0)) {
  case 1:
    return false;
  case 2:
    return true;
  }
#if wxCHECK_VERSION(3, 3, 0) && defined(__WXMSW__)
  return wxSystemSettings::GetAppearance().AreAppsDark();
#else
  return wxSystemSettings::GetAppearance().IsDark();
#endif
}

Skin make_skin(SkinId id, wxWindow *win)
{
  Skin s;
  auto px= [win](int dip) { return win? win->FromDIP(dip): dip; };

  s.id= id;
  if(id==SkinId::Classic) {
    s.dark= wxSystemSettings::GetAppearance().IsDark();
    return s; /* native controls: nothing to draw */
  }
  s.dark= want_dark();

  if(id==SkinId::Win11) {
    /* Not "Segoe UI Variable": GDI+ (wxGraphicsContext on Windows) drops some glyphs of variable fonts */
    wxString face= first_face({"Segoe UI", "Selawik", "Noto Sans", "Cantarell"});

    s.font= make_font(face, 10, wxFONTWEIGHT_NORMAL); /* whole points: GDI+ lost "." glyphs at 9.5 */
    s.header_font= make_font(face, 9, wxFONTWEIGHT_NORMAL);
    s.bar_font= make_font(face, 9, wxFONTWEIGHT_NORMAL);
    s.row_h= px(32);
    s.header_h= px(32);
    s.bar_h= px(28);
    s.pad= px(12);
    s.radius= px(4);
    s.bar_thickness= px(3);
    s.pill= true;
    s.inset_select= true;
    s.status_colors= true;
    if(s.dark) {
      s.bg= wxColour(0x27, 0x27, 0x27);
      s.text= wxColour(0xFF, 0xFF, 0xFF);
      s.text2= wxColour(0xC5, 0xC5, 0xC5);
      s.header_bg= wxColour(0x20, 0x20, 0x20);
      s.header_text= wxColour(0xC5, 0xC5, 0xC5);
      s.header_line= wxColour(0x3A, 0x3A, 0x3A);
      s.hover= wxColour(0x32, 0x32, 0x32);
      s.select= wxColour(0x38, 0x38, 0x38);
      s.select_text= s.text;
      s.accent= wxColour(0x60, 0xCD, 0xFF);
      s.track= wxColour(0x5C, 0x5C, 0x5C);
      s.ok= wxColour(0x6C, 0xCB, 0x5F);
      s.bad= wxColour(0xFF, 0x99, 0xA4);
      s.bar_bg= wxColour(0x20, 0x20, 0x20);
      s.bar_text= wxColour(0xC5, 0xC5, 0xC5);
      s.bar_line= wxColour(0x3A, 0x3A, 0x3A);
    } else {
      s.bg= wxColour(0xFF, 0xFF, 0xFF);
      s.text= wxColour(0x1A, 0x1A, 0x1A);
      s.text2= wxColour(0x5F, 0x5F, 0x5F);
      s.header_bg= wxColour(0xF9, 0xF9, 0xF9);
      s.header_text= wxColour(0x5F, 0x5F, 0x5F);
      s.header_line= wxColour(0xE5, 0xE5, 0xE5);
      s.hover= wxColour(0xF5, 0xF5, 0xF5);
      s.select= wxColour(0xEB, 0xEB, 0xEB);
      s.select_text= s.text;
      s.accent= wxColour(0x00, 0x5F, 0xB8);
      s.track= wxColour(0xD1, 0xD1, 0xD1);
      s.ok= wxColour(0x0F, 0x7B, 0x0F);
      s.bad= wxColour(0xC4, 0x2B, 0x1C);
      s.bar_bg= wxColour(0xF3, 0xF3, 0xF3);
      s.bar_text= wxColour(0x5F, 0x5F, 0x5F);
      s.bar_line= wxColour(0xE5, 0xE5, 0xE5);
    }
    return s;
  }

  /* Material Design 1 */
  wxString face= first_face({"Roboto", "Noto Sans", "Segoe UI", "Arial"});

  s.font= make_font(face, 10, wxFONTWEIGHT_NORMAL);
  s.header_font= make_font(face, 10.5, wxFONTWEIGHT_MEDIUM);
  s.bar_font= make_font(face, 10, wxFONTWEIGHT_NORMAL);
  s.row_h= px(48);
  s.header_h= px(56);
  s.bar_h= px(32);
  s.pad= px(16);
  s.radius= 0;
  s.bar_thickness= px(4);
  s.header_shadow= true;
  s.status_colors= true;
  s.header_bg= wxColour(0x3F, 0x51, 0xB5);    /* Indigo 500 */
  s.header_text= wxColour(0xFF, 0xFF, 0xFF);
  s.accent= wxColour(0xFF, 0x40, 0x81);       /* Pink A200 */
  s.bar_bg= wxColour(0x30, 0x3F, 0x9F);       /* Indigo 700, like an Android status bar */
  s.bar_text= wxColour(0xFF, 0xFF, 0xFF);
  if(s.dark) {
    s.bg= wxColour(0x30, 0x30, 0x30);
    s.text= wxColour(0xFF, 0xFF, 0xFF);
    s.text2= wxColour(0xB3, 0xB3, 0xB3);
    s.divider= wxColour(0x4A, 0x4A, 0x4A);
    s.hover= wxColour(0x3A, 0x3A, 0x3A);
    s.select= wxColour(0x42, 0x42, 0x42);   /* Grey 800 */
    s.track= wxColour(0x5C, 0x6B, 0xC0);    /* Indigo 400 */
    s.ok= wxColour(0x81, 0xC7, 0x84);       /* Green 300 */
    s.bad= wxColour(0xE5, 0x73, 0x73);      /* Red 300 */
    s.header_bg= wxColour(0x28, 0x35, 0x93); /* Indigo 800 */
    s.bar_bg= wxColour(0x1A, 0x23, 0x7E);    /* Indigo 900 */
  } else {
    s.bg= wxColour(0xFF, 0xFF, 0xFF);
    s.text= wxColour(0x21, 0x21, 0x21);     /* 87% black */
    s.text2= wxColour(0x75, 0x75, 0x75);    /* 54% black */
    s.divider= wxColour(0xE0, 0xE0, 0xE0);
    s.hover= wxColour(0xF5, 0xF5, 0xF5);    /* Grey 100 */
    s.select= wxColour(0xE8, 0xEA, 0xF6);   /* Indigo 50 */
    s.track= wxColour(0xC5, 0xCA, 0xE9);    /* Indigo 100 */
    s.ok= wxColour(0x43, 0xA0, 0x47);       /* Green 600 */
    s.bad= wxColour(0xE5, 0x39, 0x35);      /* Red 600 */
  }
  s.select_text= s.text;
  s.header_line= s.header_bg;
  s.bar_line= s.bar_bg;
  return s;
}
