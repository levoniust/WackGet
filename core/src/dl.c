/* Downloads with libcurl, one thread each. Replaces the patched wget 1.10.2 that 1.2.x started per
 * download (list.c spawn_download) and the WM_COPYDATA progress messages it sent back: the window
 * now reads wg_dl_snapshot() once a second instead. Behaviour kept from the wget command line:
 * --tries=0 --waitretry=1 (keep retrying, 1 s apart), --continue / --timestamping, --referer=,
 * proxy and proxy login. There is no command line any more, so URLs can't inject options. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "internal.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
typedef CRITICAL_SECTION wg_mutex;
#define mutex_init(m) InitializeCriticalSection(m)
#define mutex_lock(m) EnterCriticalSection(m)
#define mutex_unlock(m) LeaveCriticalSection(m)
#define mutex_destroy(m) DeleteCriticalSection(m)
static int64_t now_ms(void) { return (int64_t)GetTickCount64(); }
static void sleep_ms(long ms) { Sleep((DWORD)ms); }
#else
#include <pthread.h>
#include <time.h>
typedef pthread_mutex_t wg_mutex;
#define mutex_init(m) pthread_mutex_init(m, NULL)
#define mutex_lock(m) pthread_mutex_lock(m)
#define mutex_unlock(m) pthread_mutex_unlock(m)
#define mutex_destroy(m) pthread_mutex_destroy(m)
static int64_t now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
}
static void sleep_ms(long ms)
{
  struct timespec ts;

  ts.tv_sec= ms/1000;
  ts.tv_nsec= (ms%1000)*1000000L;
  nanosleep(&ts, NULL);
}
#endif

#define USER_AGENT "WackGet/" WG_VERSION

struct wg_dl {
  wg_mutex lock;
#ifdef _WIN32
  HANDLE thread;
#else
  pthread_t thread;
#endif
  /* options (owned copies) */
  char *url, *dir, *proxy, *proxy_user, *proxy_pass, *referer;
  int resume, max_tries;
  long retry_wait_ms;

  int has_thread;

  /* shared with the window thread, under lock */
  int stop;
  wg_dl_info info;

  /* download thread only */
  CURL *curl;
  FILE *f;
  int opened, discard, uptodate;
  int timestamping;             /* compare with the local file like wget --timestamping */
  int64_t local_size, local_mtime;
  int64_t offset, attempt_start;
  char errbuf[CURL_ERROR_SIZE];
  struct wg_dl *claim_next; /* in the list of files being written, while claimed */
};

typedef enum { R_DONE, R_RETRY, R_RESTART, R_FATAL, R_STOPPED } result_t;

/* How an attempt treats an existing local file */
enum { MODE_TIMESTAMP, MODE_CONTINUE, MODE_FRESH };

/* Files being written. Two queued links can have the same file name (the name comes from the
   URL, as with wget); they must not write the same file at the same time, so the second waits
   for the first and then goes by the usual rules (up to date, continue, or download again). */
static wg_mutex claims_lock;
static wg_dl *claims;

int wg_global_init(void)
{
  mutex_init(&claims_lock);
  return curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK? WG_OK: WG_ERR_IO;
}

void wg_global_cleanup(void)
{
  curl_global_cleanup();
  mutex_destroy(&claims_lock);
}

static int same_path(const char *a, const char *b)
{
#ifdef _WIN32
  return _stricmp(a, b)==0; /* NTFS: "A.zip" and "a.zip" are one file */
#else
  return strcmp(a, b)==0;
#endif
}

static int claim(wg_dl *dl)
{
  wg_dl *o;
  int ok= 1;

  mutex_lock(&claims_lock);
  for(o= claims; o; o= o->claim_next) {
    if(same_path(o->info.path, dl->info.path)) {
      ok= 0;
      break;
    }
  }
  if(ok) {
    dl->claim_next= claims;
    claims= dl;
  }
  mutex_unlock(&claims_lock);
  return ok;
}

static void unclaim(wg_dl *dl)
{
  wg_dl **pp;

  mutex_lock(&claims_lock);
  for(pp= &claims; *pp; pp= &(*pp)->claim_next) {
    if(*pp==dl) {
      *pp= dl->claim_next;
      break;
    }
  }
  mutex_unlock(&claims_lock);
}

static int stop_requested(wg_dl *dl)
{
  int s;

  mutex_lock(&dl->lock);
  s= dl->stop;
  mutex_unlock(&dl->lock);
  return s;
}

static void set_message(wg_dl *dl, const char *msg)
{
  mutex_lock(&dl->lock);
  snprintf(dl->info.message, sizeof(dl->info.message), "%s", msg);
  mutex_unlock(&dl->lock);
}

static int is_http(wg_dl *dl)
{
  char *scheme= NULL;

  curl_easy_getinfo(dl->curl, CURLINFO_SCHEME, &scheme);
  return scheme && (wg_prefix_ci(scheme, strlen(scheme), "http"));
}

/* First body bytes of a response: decide whether and how to open the file. */
static int open_output(wg_dl *dl)
{
  long code= 0;
  curl_off_t cl= -1;
  const char *mode;

  dl->opened= 1;
  curl_easy_getinfo(dl->curl, CURLINFO_RESPONSE_CODE, &code);
  if(is_http(dl)) {
    if(code!=200 && code!=206) {
      dl->discard= 1; /* error page: don't write it into the file */
      return 0;
    }
    if(code==200 && dl->offset>0) {
      dl->offset= 0; /* server ignored the range: start over */
    }
  }
  curl_easy_getinfo(dl->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
  if(dl->timestamping) { /* wget -N: same date (or older) and same size = already have it */
    curl_off_t ft= -1;

    curl_easy_getinfo(dl->curl, CURLINFO_FILETIME_T, &ft);
    if(ft>=0 && ft<=dl->local_mtime && cl>=0 && cl==dl->local_size) {
      dl->uptodate= 1;
      return -1; /* abort the transfer; classify() reports it as done */
    }
  }
  mode= (dl->offset>0)? "ab": "wb";
  if(!(dl->f= wg_fs_fopen(dl->info.path, mode))) {
    set_message(dl, "Can't write the file");
    return -1;
  }
  mutex_lock(&dl->lock);
  dl->info.prog.start= dl->info.prog.cur= dl->offset;
  dl->info.prog.total= (cl>=0)? dl->offset+cl: 0;
  mutex_unlock(&dl->lock);
  return 0;
}

static size_t on_write(char *data, size_t size, size_t nmemb, void *userp)
{
  wg_dl *dl= userp;
  size_t n= size*nmemb;

  if(!dl->opened && open_output(dl)<0) {
    return 0; /* CURLE_WRITE_ERROR */
  }
  if(dl->discard) {
    return n;
  }
  if(fwrite(data, 1, n, dl->f)!=n) {
    set_message(dl, "Can't write the file (disk full?)");
    return 0;
  }
  mutex_lock(&dl->lock);
  dl->info.prog.cur+= (int64_t)n;
  dl->info.prog.elapsed= now_ms()-dl->attempt_start;
  mutex_unlock(&dl->lock);
  return n;
}

static int on_progress(void *userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
  wg_dl *dl= userp;
  int stop;

  (void)dltotal;
  (void)dlnow;
  (void)ultotal;
  (void)ulnow;
  mutex_lock(&dl->lock);
  stop= dl->stop;
  if(dl->opened) {
    dl->info.prog.elapsed= now_ms()-dl->attempt_start;
  }
  mutex_unlock(&dl->lock);
  return stop; /* non-zero aborts the transfer */
}

static result_t up_to_date(wg_dl *dl)
{
  mutex_lock(&dl->lock); /* the local copy is current (wget -N "not retrieving") */
  dl->info.prog.start= dl->info.prog.cur= dl->info.prog.total= dl->local_size;
  mutex_unlock(&dl->lock);
  set_message(dl, "The local copy is already up to date");
  return R_DONE;
}

static result_t classify(wg_dl *dl, CURLcode rc)
{
  long code= 0;
  int http;

  curl_easy_getinfo(dl->curl, CURLINFO_RESPONSE_CODE, &code);
  http= is_http(dl);
  if(rc==CURLE_ABORTED_BY_CALLBACK && stop_requested(dl)) {
    return R_STOPPED;
  }
  if(dl->uptodate) {
    return up_to_date(dl);
  }
  if(rc==CURLE_RANGE_ERROR) {
    set_message(dl, "The server can't resume; starting over");
    return R_RESTART;
  }
  if(rc==CURLE_OK) {
    if(!http || code==200 || code==206) {
      if(!dl->opened && open_output(dl)<0) { /* empty file: still create it */
        return dl->uptodate? up_to_date(dl): R_FATAL;
      }
      return R_DONE;
    }
    if(code==416 && dl->offset>0) {
      int64_t size= 0;

      wg_fs_stat(dl->info.path, &size, NULL); /* already complete (same as wget --continue) */
      mutex_lock(&dl->lock);
      dl->info.prog.start= dl->info.prog.cur= dl->info.prog.total= size;
      mutex_unlock(&dl->lock);
      return R_DONE;
    }
    mutex_lock(&dl->lock);
    dl->info.prog.error= (int)code;
    mutex_unlock(&dl->lock);
    {
      char msg[64];

      snprintf(msg, sizeof(msg), "HTTP error %ld", code);
      set_message(dl, msg);
    }
    return (code==408 || code==429 || code>=500)? R_RETRY: R_FATAL;
  }
  set_message(dl, dl->errbuf[0]? dl->errbuf: curl_easy_strerror(rc));
  switch(rc) {
  case CURLE_UNSUPPORTED_PROTOCOL:
  case CURLE_URL_MALFORMAT:
  case CURLE_COULDNT_RESOLVE_HOST:
  case CURLE_COULDNT_RESOLVE_PROXY:
  case CURLE_REMOTE_ACCESS_DENIED:
  case CURLE_REMOTE_FILE_NOT_FOUND:
  case CURLE_LOGIN_DENIED:
  case CURLE_TOO_MANY_REDIRECTS:
  case CURLE_PEER_FAILED_VERIFICATION:
  case CURLE_SSL_CACERT_BADFILE:
  case CURLE_SSL_CERTPROBLEM:
  case CURLE_SSL_ISSUER_ERROR:
  case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
  case CURLE_WRITE_ERROR:
  case CURLE_FILESIZE_EXCEEDED:
    return R_FATAL;
  default:
    return R_RETRY; /* timeouts, dropped connections, partial transfers: try again */
  }
}

static result_t attempt(wg_dl *dl, int mode)
{
  int64_t size= 0, mtime= 0;
  int exists;
  CURLcode rc;
  result_t r;

  exists= wg_fs_stat(dl->info.path, &size, &mtime)==0;
  if(!(dl->curl= curl_easy_init())) {
    set_message(dl, "libcurl failed to start");
    return R_FATAL;
  }
  dl->f= NULL;
  dl->opened= dl->discard= dl->uptodate= 0;
  dl->offset= 0;
  dl->timestamping= (mode==MODE_TIMESTAMP && exists);
  dl->local_size= size;
  dl->local_mtime= mtime;
  dl->errbuf[0]= '\0';
  dl->attempt_start= now_ms();

  curl_easy_setopt(dl->curl, CURLOPT_URL, dl->url);
  curl_easy_setopt(dl->curl, CURLOPT_PROTOCOLS_STR, "http,https,ftp");
  curl_easy_setopt(dl->curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https,ftp"); /* never file: etc. */
  curl_easy_setopt(dl->curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(dl->curl, CURLOPT_MAXREDIRS, 20L);
  curl_easy_setopt(dl->curl, CURLOPT_USERAGENT, USER_AGENT);
  if(dl->referer) {
    curl_easy_setopt(dl->curl, CURLOPT_REFERER, dl->referer);
  }
  curl_easy_setopt(dl->curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(dl->curl, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(dl->curl, CURLOPT_LOW_SPEED_LIMIT, 1L); /* stalled for a minute: retry */
  curl_easy_setopt(dl->curl, CURLOPT_LOW_SPEED_TIME, 60L);
  curl_easy_setopt(dl->curl, CURLOPT_FILETIME, 1L);
  curl_easy_setopt(dl->curl, CURLOPT_ERRORBUFFER, dl->errbuf);
  curl_easy_setopt(dl->curl, CURLOPT_WRITEFUNCTION, on_write);
  curl_easy_setopt(dl->curl, CURLOPT_WRITEDATA, dl);
  curl_easy_setopt(dl->curl, CURLOPT_XFERINFOFUNCTION, on_progress);
  curl_easy_setopt(dl->curl, CURLOPT_XFERINFODATA, dl);
  curl_easy_setopt(dl->curl, CURLOPT_NOPROGRESS, 0L);
#ifdef _WIN32
  curl_easy_setopt(dl->curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
#endif
  if(dl->proxy && dl->proxy[0]) {
    curl_easy_setopt(dl->curl, CURLOPT_PROXY, dl->proxy);
    if(dl->proxy_user && dl->proxy_user[0]) {
      curl_easy_setopt(dl->curl, CURLOPT_PROXYUSERNAME, dl->proxy_user);
    }
    if(dl->proxy_pass && dl->proxy_pass[0]) {
      curl_easy_setopt(dl->curl, CURLOPT_PROXYPASSWORD, dl->proxy_pass);
    }
  }
  if(mode==MODE_CONTINUE && exists && size>0) {
    dl->offset= size; /* --continue */
    curl_easy_setopt(dl->curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)size);
  }

  rc= curl_easy_perform(dl->curl);
  if(dl->f) {
    if(fclose(dl->f)!=0 && rc==CURLE_OK) {
      rc= CURLE_WRITE_ERROR;
    }
    dl->f= NULL;
  }
  r= classify(dl, rc);
  if(r==R_DONE) {
    curl_off_t ft= -1;

    curl_easy_getinfo(dl->curl, CURLINFO_FILETIME_T, &ft);
    if(ft>=0 && dl->opened && !dl->discard) {
      wg_fs_set_mtime(dl->info.path, (int64_t)ft); /* keep the server's date, like wget */
    }
  }
  curl_easy_cleanup(dl->curl);
  dl->curl= NULL;
  return r;
}

static void finish(wg_dl *dl, wg_dl_state st)
{
  mutex_lock(&dl->lock);
  dl->info.state= st;
  if(st==WG_DL_DONE && dl->info.prog.total<dl->info.prog.cur) {
    dl->info.prog.total= dl->info.prog.cur; /* size was unknown: now it isn't */
  }
  mutex_unlock(&dl->lock);
}

static void run_claimed(wg_dl *dl);

static void run(wg_dl *dl)
{
  if(!claim(dl)) {
    set_message(dl, "Waiting: another download is writing this file");
    while(!claim(dl)) {
      if(stop_requested(dl)) {
        finish(dl, WG_DL_STOPPED);
        return;
      }
      sleep_ms(200);
    }
    set_message(dl, "");
  }
  run_claimed(dl);
  unclaim(dl);
}

static void run_claimed(wg_dl *dl)
{
  int tries= 0, mode= dl->resume? MODE_CONTINUE: MODE_TIMESTAMP;

  wg_fs_mkdirs(dl->dir);
  for(;;) {
    result_t r;

    if(stop_requested(dl)) {
      finish(dl, WG_DL_STOPPED);
      return;
    }
    tries++;
    r= attempt(dl, mode);
    if(r==R_DONE) {
      finish(dl, WG_DL_DONE);
      return;
    } else if(r==R_STOPPED) {
      finish(dl, WG_DL_STOPPED);
      return;
    } else if(r==R_FATAL || (dl->max_tries>0 && tries>=dl->max_tries)) {
      finish(dl, WG_DL_FAILED);
      return;
    }
    mode= (r==R_RESTART)? MODE_FRESH: MODE_CONTINUE; /* retries continue the partial file */
    {
      long waited;

      for(waited= 0; waited<dl->retry_wait_ms && !stop_requested(dl); waited+= 50) {
        sleep_ms(50);
      }
    }
  }
}

#ifdef _WIN32
static unsigned __stdcall thread_main(void *p)
{
  run(p);
  return 0;
}
#else
static void *thread_main(void *p)
{
  run(p);
  return NULL;
}
#endif

static char *dup_or_null(const char *s)
{
  return (s && *s)? wg_strdup(s): NULL;
}

wg_dl *wg_dl_start(const wg_dl_opts *o)
{
  wg_dl *dl= wg_malloc(sizeof(*dl));
  char *name, *path;
  int started;

  memset(dl, 0, sizeof(*dl));
  mutex_init(&dl->lock);
  dl->url= wg_strdup(o->url? o->url: "");
  dl->dir= wg_strdup(o->dir && *o->dir? o->dir: ".");
  dl->proxy= dup_or_null(o->proxy);
  dl->proxy_user= dup_or_null(o->proxy_user);
  dl->proxy_pass= dup_or_null(o->proxy_pass);
  dl->referer= wg_url_referer(dl->url);
  dl->resume= o->resume;
  dl->max_tries= o->max_tries;
  dl->retry_wait_ms= (o->retry_wait_ms>0)? o->retry_wait_ms: 1000;

  name= wg_fs_name_from_url(dl->url);
#ifdef _WIN32
  /* Keep the whole path under Windows' classic 260 characters where the folder allows, so
     Explorer and older programs can open the file (UTF-8 bytes >= UTF-16 units: safe count). */
  if(strlen(dl->dir)+1+strlen(name)>259 && strlen(dl->dir)+1+40<=259) {
    wg_fs_fit_name(name, 259-strlen(dl->dir)-1);
  }
#endif
  path= wg_fs_join(dl->dir, name);
  snprintf(dl->info.name, sizeof(dl->info.name), "%s", name);
  snprintf(dl->info.path, sizeof(dl->info.path), "%s", path);
  wg_free(name);
  wg_free(path);
  dl->info.state= WG_DL_RUNNING;

  if(!wg_url_is_supported(dl->url)) {
    dl->info.state= WG_DL_FAILED;
    snprintf(dl->info.message, sizeof(dl->info.message), "Not a supported URL");
    started= 0;
  } else {
#ifdef _WIN32
    dl->thread= (HANDLE)_beginthreadex(NULL, 0, thread_main, dl, 0, NULL);
    started= dl->thread!=NULL;
#else
    started= pthread_create(&dl->thread, NULL, thread_main, dl)==0;
#endif
    if(!started) {
      dl->info.state= WG_DL_FAILED;
      snprintf(dl->info.message, sizeof(dl->info.message), "Couldn't start a download thread");
    }
  }
  dl->has_thread= started;
  return dl;
}

void wg_dl_snapshot(wg_dl *dl, wg_dl_info *out)
{
  mutex_lock(&dl->lock);
  *out= dl->info;
  mutex_unlock(&dl->lock);
}

void wg_dl_stop(wg_dl *dl)
{
  mutex_lock(&dl->lock);
  dl->stop= 1;
  mutex_unlock(&dl->lock);
}

void wg_dl_free(wg_dl *dl)
{
  if(!dl) {
    return;
  }
  if(dl->has_thread) {
#ifdef _WIN32
    WaitForSingleObject(dl->thread, INFINITE);
    CloseHandle(dl->thread);
#else
    pthread_join(dl->thread, NULL);
#endif
  }
  mutex_destroy(&dl->lock);
  wg_free(dl->url);
  wg_free(dl->dir);
  wg_free(dl->proxy);
  wg_free(dl->proxy_user);
  wg_free(dl->proxy_pass);
  wg_free(dl->referer);
  wg_free(dl);
}
