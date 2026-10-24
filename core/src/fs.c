/* Files, folders and file names. UTF-8 paths everywhere; Windows goes through the wide APIs. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#define SEP '\\'
#else
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#define SEP '/'
#endif

/* ---------------------------------------------------------------- file names */

static int hexval(char c)
{
  return (c>='0' && c<='9')? c-'0': (c>='a' && c<='f')? c-'a'+10: (c>='A' && c<='F')? c-'A'+10: -1;
}

/* Length of a valid UTF-8 sequence at s (at most n bytes), or 0. */
static size_t utf8_len(const unsigned char *s, size_t n)
{
  size_t need, i;
  unsigned long cp;

  if(s[0]<0x80) {
    return 1;
  } else if((s[0]&0xe0)==0xc0) {
    need= 2;
    cp= s[0]&0x1f;
  } else if((s[0]&0xf0)==0xe0) {
    need= 3;
    cp= s[0]&0x0f;
  } else if((s[0]&0xf8)==0xf0) {
    need= 4;
    cp= s[0]&0x07;
  } else {
    return 0;
  }
  if(n<need) {
    return 0;
  }
  for(i= 1; i<need; i++) {
    if((s[i]&0xc0)!=0x80) {
      return 0;
    }
    cp= (cp<<6)|(s[i]&0x3f);
  }
  if((need==2 && cp<0x80) || (need==3 && cp<0x800) || (need==4 && (cp<0x10000 || cp>0x10ffff)) ||
     (cp>=0xd800 && cp<=0xdfff)) {
    return 0; /* overlong or invalid */
  }
  return need;
}

static int is_reserved_windows_name(const char *name)
{
  static const char *const dev[]= {"con", "prn", "aux", "nul"};
  size_t base= strcspn(name, "."), i;

  while(base>0 && name[base-1]==' ') {
    base--;
  }
  for(i= 0; i<4; i++) {
    if(base==3 && wg_prefix_ci(name, base, dev[i])) {
      return 1;
    }
  }
  return base==4 && (wg_prefix_ci(name, 4, "com") || wg_prefix_ci(name, 4, "lpt")) &&
    name[3]>='0' && name[3]<='9';
}

#define WG_NAME_MAX 240

/* Cut a name to at most max bytes, keeping a short extension and cutting on a UTF-8 boundary.
   Returns the new length. */
static size_t shorten(char *p, size_t len, size_t max)
{
  const char *dot= strrchr(p, '.');
  size_t ext, keep;

  if(len<=max) {
    return len;
  }
  ext= (dot && len-(size_t)(dot-p)<=16)? len-(size_t)(dot-p): 0;
  keep= max-ext;
  while(keep>0 && ((unsigned char)p[keep]&0xc0)==0x80) {
    keep--;
  }
  memmove(p+keep, p+len-ext, ext+1);
  return keep+ext;
}

void wg_fs_fit_name(char *name, size_t max)
{
  size_t len= shorten(name, strlen(name), max);

  while(len>0 && (name[len-1]=='.' || name[len-1]==' ')) {
    name[--len]= '\0';
  }
}

char *wg_fs_name_from_url(const char *url)
{
  const char *path, *seg, *q;
  size_t n, qn, i;
  wg_buf raw= {0}, out= {0};

  path= strstr(url, "://");
  path= path? path+3: url;
  path+= strcspn(path, "/?#"); /* skip the host */
  n= strcspn(path, "?#");
  seg= path+n;
  while(seg>path && seg[-1]!='/') {
    seg--;
  }
  /* The query stays part of the name, '?' becoming '@', as the Windows wget of 1.2.x did
     ("download.php@id=7"): different links then get different files. */
  q= (path[n]=='?')? path+n+1: NULL;
  qn= q? strcspn(q, "#"): 0;
  n= (size_t)(path+n-seg);

  /* percent-decode */
  wg_buf_add(&raw, "", 0);
  if(n==0 && qn>0) {
    seg= "index.html"; /* "http://h/?x=1" -> "index.html@x=1", like wget */
    n= strlen(seg);
  }
  for(i= 0; i<n; i++) {
    if(seg[i]=='%' && i+2<n && hexval(seg[i+1])>=0 && hexval(seg[i+2])>=0) {
      wg_buf_addc(&raw, (char)(hexval(seg[i+1])*16+hexval(seg[i+2])));
      i+= 2;
    } else {
      wg_buf_addc(&raw, seg[i]);
    }
  }
  if(qn>0) {
    wg_buf_addc(&raw, '@');
    for(i= 0; i<qn; i++) {
      if(q[i]=='%' && i+2<qn && hexval(q[i+1])>=0 && hexval(q[i+2])>=0) {
        wg_buf_addc(&raw, (char)(hexval(q[i+1])*16+hexval(q[i+2])));
        i+= 2;
      } else {
        wg_buf_addc(&raw, q[i]);
      }
    }
  }

  /* keep valid UTF-8 and characters every OS allows */
  wg_buf_add(&out, "", 0);
  for(i= 0; i<raw.len; ) {
    unsigned char c= (unsigned char)raw.p[i];
    size_t l= utf8_len((const unsigned char *)raw.p+i, raw.len-i);

    if(l==0) {
      wg_buf_addc(&out, '_');
      i++;
    } else if(l>1) {
      wg_buf_add(&out, raw.p+i, l);
      i+= l;
    } else {
      wg_buf_addc(&out, (c<0x20 || c==0x7f || strchr("/\\:*?\"<>|", c))? '_': (char)c);
      i++;
    }
  }
  wg_free(raw.p);

  /* Shorten first (one byte spare for the '_' below), then trim, so a cut can't leave a trailing
     space or dot. */
  out.len= shorten(out.p, out.len, WG_NAME_MAX-1);

  /* no leading spaces, no trailing dots or spaces (Windows drops them) */
  i= 0;
  while(out.p[i]==' ') {
    i++;
  }
  if(i) {
    memmove(out.p, out.p+i, out.len-i+1);
    out.len-= i;
  }
  while(out.len>0 && (out.p[out.len-1]=='.' || out.p[out.len-1]==' ')) {
    out.p[--out.len]= '\0';
  }
  if(out.len==0) {
    wg_free(out.p);
    return wg_strdup("index.html");
  }
  if(is_reserved_windows_name(out.p)) {
    wg_buf b= {0};

    wg_buf_addc(&b, '_');
    wg_buf_add(&b, out.p, out.len);
    wg_free(out.p);
    out= b;
  }
  return out.p;
}

char *wg_fs_join(const char *dir, const char *name)
{
  size_t dn= strlen(dir);
  wg_buf b= {0};

  wg_buf_add(&b, dir, dn);
  if(dn>0 && dir[dn-1]!='/' && dir[dn-1]!='\\') {
    wg_buf_addc(&b, SEP);
  }
  wg_buf_adds(&b, name);
  return b.p;
}

/* ---------------------------------------------------------------- Windows */
#ifdef _WIN32

wchar_t *wg_utf8_to_wide(const char *s)
{
  int n= MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t *w;

  if(n<=0) {
    w= wg_malloc(sizeof(wchar_t));
    w[0]= 0;
    return w;
  }
  w= wg_malloc((size_t)n*sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

char *wg_wide_to_utf8(const wchar_t *w)
{
  int n= WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *s;

  if(n<=0) {
    return wg_strdup("");
  }
  s= wg_malloc((size_t)n);
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
  return s;
}

/* A path for the wide file calls: past the classic 260-character limit (248 for folders), use the
   \\?\ form, which has no such limit (it needs a full path with backslashes). */
static wchar_t *wide_path(const char *path)
{
  wchar_t *w= wg_utf8_to_wide(path), *full, *out;
  DWORD n;

  if(wcslen(w)<248 || wcsncmp(w, L"\\\\?\\", 4)==0) {
    return w;
  }
  n= GetFullPathNameW(w, 0, NULL, NULL);
  if(n==0) {
    return w;
  }
  full= wg_malloc((size_t)n*sizeof(wchar_t));
  GetFullPathNameW(w, n, full, NULL);
  wg_free(w);
  out= wg_malloc(((size_t)n+8)*sizeof(wchar_t));
  if(full[0]==L'\\' && full[1]==L'\\') { /* \\server\share -> \\?\UNC\server\share */
    wcscpy(out, L"\\\\?\\UNC");
    wcscat(out, full+1);
  } else {
    wcscpy(out, L"\\\\?\\");
    wcscat(out, full);
  }
  wg_free(full);
  return out;
}

void *wg_fs_fopen(const char *path, const char *mode)
{
  wchar_t *wp= wide_path(path), *wm= wg_utf8_to_wide(mode);
  FILE *f= _wfopen(wp, wm);

  wg_free(wp);
  wg_free(wm);
  return f;
}

int wg_fs_exists(const char *path)
{
  wchar_t *wp= wide_path(path);
  DWORD a= GetFileAttributesW(wp);

  wg_free(wp);
  return a!=INVALID_FILE_ATTRIBUTES;
}

/* 100 ns ticks since 1601 <-> Unix seconds */
#define EPOCH_DIFF 11644473600LL

int wg_fs_stat(const char *path, int64_t *size, int64_t *mtime)
{
  wchar_t *wp= wide_path(path);
  WIN32_FILE_ATTRIBUTE_DATA d;
  BOOL ok= GetFileAttributesExW(wp, GetFileExInfoStandard, &d);

  wg_free(wp);
  if(!ok || (d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
    return -1;
  }
  if(size) {
    *size= ((int64_t)d.nFileSizeHigh<<32)|d.nFileSizeLow;
  }
  if(mtime) {
    int64_t t= ((int64_t)d.ftLastWriteTime.dwHighDateTime<<32)|d.ftLastWriteTime.dwLowDateTime;

    *mtime= t/10000000-EPOCH_DIFF;
  }
  return 0;
}

int wg_fs_set_mtime(const char *path, int64_t mtime)
{
  wchar_t *wp= wide_path(path);
  HANDLE h= CreateFileW(wp, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
  FILETIME ft;
  int64_t t= (mtime+EPOCH_DIFF)*10000000;
  BOOL ok;

  wg_free(wp);
  if(h==INVALID_HANDLE_VALUE) {
    return -1;
  }
  ft.dwLowDateTime= (DWORD)t;
  ft.dwHighDateTime= (DWORD)(t>>32);
  ok= SetFileTime(h, NULL, NULL, &ft);
  CloseHandle(h);
  return ok? 0: -1;
}

int wg_fs_remove(const char *path)
{
  wchar_t *wp= wide_path(path);
  BOOL ok= DeleteFileW(wp);
  DWORD err= GetLastError();

  wg_free(wp);
  return (ok || err==ERROR_FILE_NOT_FOUND || err==ERROR_PATH_NOT_FOUND)? 0: -1;
}

static int mkdir_one(const char *path)
{
  wchar_t *wp= wide_path(path);
  BOOL ok= CreateDirectoryW(wp, NULL);
  DWORD err= GetLastError();

  wg_free(wp);
  return (ok || err==ERROR_ALREADY_EXISTS)? 0: -1;
}

int wg_replace_file(const char *tmp, const char *path)
{
  wchar_t *wt= wide_path(tmp), *wp= wide_path(path);
  BOOL ok= MoveFileExW(wt, wp, MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);

  wg_free(wt);
  wg_free(wp);
  return ok? 0: -1;
}

static char *known_folder(const KNOWNFOLDERID *id)
{
  PWSTR w= NULL;
  char *s= NULL;

  if(SHGetKnownFolderPath(id, KF_FLAG_CREATE, NULL, &w)==S_OK) {
    s= wg_wide_to_utf8(w);
  }
  CoTaskMemFree(w);
  return s;
}

char *wg_default_download_dir(void)
{
  char *s= known_folder(&FOLDERID_Downloads);

  return s? s: known_folder(&FOLDERID_Profile);
}

char *wg_config_dir(void)
{
  char *base= known_folder(&FOLDERID_RoamingAppData), *dir;

  if(!base) {
    return NULL;
  }
  dir= wg_fs_join(base, "WackGet");
  wg_free(base);
  wg_fs_mkdirs(dir);
  return dir;
}

/* ---------------------------------------------------------------- Linux and other POSIX */
#else

void *wg_fs_fopen(const char *path, const char *mode)
{
  return fopen(path, mode);
}

int wg_fs_exists(const char *path)
{
  struct stat st;

  return stat(path, &st)==0;
}

int wg_fs_stat(const char *path, int64_t *size, int64_t *mtime)
{
  struct stat st;

  if(stat(path, &st)!=0 || !S_ISREG(st.st_mode)) {
    return -1;
  }
  if(size) {
    *size= (int64_t)st.st_size;
  }
  if(mtime) {
    *mtime= (int64_t)st.st_mtime;
  }
  return 0;
}

int wg_fs_set_mtime(const char *path, int64_t mtime)
{
  struct timeval tv[2];

  tv[0].tv_sec= tv[1].tv_sec= (time_t)mtime;
  tv[0].tv_usec= tv[1].tv_usec= 0;
  return utimes(path, tv);
}

int wg_fs_remove(const char *path)
{
  return (unlink(path)==0 || errno==ENOENT)? 0: -1;
}

static int mkdir_one(const char *path)
{
  return (mkdir(path, 0755)==0 || errno==EEXIST)? 0: -1;
}

int wg_replace_file(const char *tmp, const char *path)
{
  return rename(tmp, path);
}

/* $XDG_<name>_DIR from ~/.config/user-dirs.dirs, e.g. XDG_DOWNLOAD_DIR="$HOME/Downloads" */
static char *xdg_user_dir(const char *key, const char *home)
{
  char *cfg, line[1024], *result= NULL;
  const char *xch= getenv("XDG_CONFIG_HOME");
  FILE *f;
  size_t kn= strlen(key);

  if(xch && *xch) {
    cfg= wg_fs_join(xch, "user-dirs.dirs");
  } else {
    char *c= wg_fs_join(home, ".config");

    cfg= wg_fs_join(c, "user-dirs.dirs");
    wg_free(c);
  }
  if((f= fopen(cfg, "r"))) {
    while(!result && fgets(line, sizeof(line), f)) {
      char *v, *end;

      if(strncmp(line, key, kn)!=0 || line[kn]!='=' || line[kn+1]!='"') {
        continue;
      }
      v= line+kn+2;
      if(!(end= strchr(v, '"'))) {
        continue;
      }
      *end= '\0';
      if(strncmp(v, "$HOME", 5)==0) {
        wg_buf b= {0};

        wg_buf_adds(&b, home);
        wg_buf_adds(&b, v+5);
        result= b.p;
      } else if(v[0]=='/') {
        result= wg_strdup(v);
      }
    }
    fclose(f);
  }
  wg_free(cfg);
  return result;
}

char *wg_default_download_dir(void)
{
  const char *home= getenv("HOME");
  char *d;

  if(!home || !*home) {
    return wg_strdup(".");
  }
  if((d= xdg_user_dir("XDG_DOWNLOAD_DIR", home))) {
    return d;
  }
  d= wg_fs_join(home, "Downloads");
  if(wg_fs_exists(d)) {
    return d;
  }
  wg_free(d);
  return wg_strdup(home);
}

char *wg_config_dir(void)
{
  const char *xch= getenv("XDG_CONFIG_HOME"), *home= getenv("HOME");
  char *base, *dir;

  if(xch && *xch) {
    base= wg_strdup(xch);
  } else if(home && *home) {
    base= wg_fs_join(home, ".config");
  } else {
    return NULL;
  }
  dir= wg_fs_join(base, "wackget");
  wg_free(base);
  wg_fs_mkdirs(dir);
  return dir;
}

#endif

int wg_fs_mkdirs(const char *path)
{
  char *p= wg_strdup(path), *s;
  int rv;

  for(s= p+1; *s; s++) {
    if((*s=='/' || *s=='\\') && s[-1]!=':') {
      char c= *s;

      *s= '\0';
      mkdir_one(p);
      *s= c;
    }
  }
  rv= mkdir_one(p);
  wg_free(p);
  return rv;
}

char *wg_config_path(void)
{
  char *dir= wg_config_dir(), *p;

  if(!dir) {
    return NULL;
  }
  p= wg_fs_join(dir, "wackget.ini");
  wg_free(dir);
  return p;
}
