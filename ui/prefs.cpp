/*
 * Settings file + proxy password store.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#include "prefs.h"

#include "wackget/core.h"

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#elif defined(WG_HAVE_LIBSECRET)
#include <libsecret/secret.h>
#endif

namespace prefs {

static wg_config *cfg;
static std::string cfg_path, cfg_dir, session_pass;

void init(const char *path)
{
  cfg= wg_config_new();
  if(path) {
    cfg_path= path;
    size_t s= cfg_path.find_last_of("/\\");
    cfg_dir= (s==std::string::npos)? std::string("."): cfg_path.substr(0, s);
  } else {
    char *p= wg_config_path(), *d= wg_config_dir();

    cfg_path= p? p: "";
    cfg_dir= d? d: ".";
    wg_free(p);
    wg_free(d);
  }
  if(!cfg_path.empty()) {
    wg_config_load(cfg, cfg_path.c_str());
  }
}

void cleanup()
{
  wg_config_free(cfg);
  cfg= nullptr;
}

int get_int(const char *name, int dfl)
{
  return wg_config_get_int(cfg, name, dfl);
}

std::string get_str(const char *name, const char *dfl)
{
  const char *v= wg_config_get(cfg, name, dfl);

  return v? v: "";
}

void set_int(const char *name, int val)
{
  wg_config_set_int(cfg, name, val);
}

void set_str(const char *name, const std::string &val)
{
  wg_config_set(cfg, name, val.c_str());
}

void save()
{
  if(!cfg_path.empty()) {
    wg_config_save(cfg, cfg_path.c_str());
  }
}

std::string dir()
{
  return cfg_dir;
}

std::string download_dir()
{
  std::string d= get_str("DownloadDir", "");

  while(d.size()>1 && (d.back()=='/' || d.back()=='\\')) { /* fix_download_dir() */
    d.pop_back();
  }
  if(d.empty()) {
    char *def= wg_default_download_dir();

    d= def;
    wg_free(def);
  }
  return d;
}

#ifdef _WIN32

static const wchar_t cred_target[]= L"WackGet/Proxy";

static std::wstring widen(const std::string &s)
{
  int n= MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n>0? (size_t)n: 1, L'\0');

  if(n>0) {
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  }
  w.resize(wcslen(w.c_str()));
  return w;
}

bool secret_available()
{
  return true;
}

bool secret_store(const std::string &user, const std::string &pass)
{
  if(pass.empty()) {
    CredDeleteW(cred_target, CRED_TYPE_GENERIC, 0);
    return true;
  }
  std::wstring wuser= widen(user.empty()? std::string("proxy"): user);
  CREDENTIALW c= {};

  c.Type= CRED_TYPE_GENERIC;
  c.TargetName= const_cast<wchar_t *>(cred_target);
  c.UserName= &wuser[0];
  c.CredentialBlobSize= (DWORD)pass.size();
  c.CredentialBlob= (LPBYTE)const_cast<char *>(pass.data()); /* UTF-8 bytes */
  c.Persist= CRED_PERSIST_LOCAL_MACHINE;                     /* this user, this PC */
  return CredWriteW(&c, 0)!=FALSE;
}

std::string secret_load()
{
  PCREDENTIALW c= nullptr;
  std::string s;

  if(CredReadW(cred_target, CRED_TYPE_GENERIC, 0, &c)) {
    s.assign((const char *)c->CredentialBlob, c->CredentialBlobSize);
    SecureZeroMemory(c->CredentialBlob, c->CredentialBlobSize);
    CredFree(c);
  }
  return s;
}

#elif defined(WG_HAVE_LIBSECRET)

static const SecretSchema *schema()
{
  static const SecretSchema s= {
    "org.wackget.Proxy", SECRET_SCHEMA_NONE,
    {{"purpose", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
    0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  return &s;
}

bool secret_available()
{
  return true;
}

bool secret_store(const std::string &, const std::string &pass)
{
  GError *err= nullptr;
  gboolean ok;

  if(pass.empty()) {
    secret_password_clear_sync(schema(), nullptr, &err, "purpose", "proxy", nullptr);
    if(err) {
      g_error_free(err);
    }
    return true;
  }
  ok= secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, "WackGet proxy password",
                                 pass.c_str(), nullptr, &err, "purpose", "proxy", nullptr);
  if(err) {
    g_error_free(err);
    session_pass= pass; /* no keyring running: keep it for this run */
    return false;
  }
  return ok;
}

std::string secret_load()
{
  GError *err= nullptr;
  gchar *p= secret_password_lookup_sync(schema(), nullptr, &err, "purpose", "proxy", nullptr);
  std::string s;

  if(err) {
    g_error_free(err);
  }
  if(p) {
    s= p;
    secret_password_free(p);
    return s;
  }
  return session_pass;
}

#else

bool secret_available()
{
  return false;
}

bool secret_store(const std::string &, const std::string &pass)
{
  session_pass= pass;
  return false;
}

std::string secret_load()
{
  return session_pass;
}

#endif

} // namespace prefs
