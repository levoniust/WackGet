/*
 * Settings, under the original value names (get_prefs_int() and friends in WGDLL), kept in the
 * core's Key=Value file instead of the registry. The proxy password goes to the OS secret store.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#ifndef WACKGET_UI_PREFS_H
#define WACKGET_UI_PREFS_H

#include <string>

namespace prefs {

/* Loads the file (path NULL = the default config path). */
void init(const char *path);
void cleanup();
int get_int(const char *name, int dfl);
std::string get_str(const char *name, const char *dfl);
void set_int(const char *name, int val);
void set_str(const char *name, const std::string &val);
void save();
/* Folder of the settings file (for WackGet.log). */
std::string dir();
/* DownloadDir, or the user's Downloads folder when it is unset. */
std::string download_dir();

/* Proxy password in Windows Credential Manager / the freedesktop Secret Service.
   store() returns false when there is no secret store (the password is then kept for this run only). */
bool secret_store(const std::string &user, const std::string &pass);
std::string secret_load();
bool secret_available();

} // namespace prefs

#endif
