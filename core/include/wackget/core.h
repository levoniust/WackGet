/*
 * WackGet core: everything that isn't window code, shared by the Windows and Linux front ends.
 * All strings are UTF-8. Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#ifndef WACKGET_CORE_H
#define WACKGET_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WG_VERSION "2.0.0-dev"
#define WG_URL_MAX 16384 /* longest URL accepted from any input (signed CDN links can be ~2-4K) */

/* ------------------------------------------------------------------ items */

/* Same values as the original status_t: they are stored in .wgq files. */
typedef enum {
  WG_STAT_QUEUED,  /* Queued, waiting to start */
  WG_STAT_GOING,   /* In progress */
  WG_STAT_PAUSED,  /* Paused (stopped) */
  WG_STAT_DONE,    /* Complete */
  WG_STAT_FAILED,  /* Failure */
  WG_STAT_COUNT
} wg_status;

#define WG_FLAG_DELETE 0x1 /* Partial file needs deleting (original flags_t) */
#define WG_FLAG_MASK   0x1

/* Progress of one download; the same fields the original kept in item_info_t.prog. */
typedef struct {
  int64_t total;   /* Total size in bytes; 0 = unknown */
  int64_t start;   /* Initial byte offset (resumed downloads) */
  int64_t cur;     /* Current byte offset */
  int64_t elapsed; /* Milliseconds elapsed */
  int error;       /* HTTP status (e.g. 404) or 0; shown as "Failed (404)" */
} wg_progress;

/* ------------------------------------------------------------------ strings and URLs */

char *wg_strdup(const char *s);             /* never returns NULL (aborts when out of memory) */
char *wg_strndup(const char *s, size_t n);
void *wg_malloc(size_t n);
void *wg_realloc(void *p, size_t n);
void wg_free(void *p);

/* 1 if url is http://, https:// or ftp:// with a host, no spaces or control characters, and at
   most WG_URL_MAX bytes. Every URL from every input goes through this. */
int wg_url_is_supported(const char *url);

/* "scheme://host[:port]/" for the Referer header, like the original (which sent the scheme and host
   so anti-leech links work). Any user:password@ part is left out. New string, or NULL. */
char *wg_url_referer(const char *url);

typedef void (*wg_url_cb)(const char *url, void *ctx);

/* Finds every supported URL in plain text (typed, pasted, dropped, text/uri-list) and calls cb for
   each. Returns how many were found. */
size_t wg_extract_urls_text(const char *text, size_t len, wg_url_cb cb, void *ctx);

/* Finds links (href=, src= and bare URLs) in HTML. Understands the Windows CF_HTML clipboard header
   (SourceURL / StartFragment) and resolves relative links against SourceURL, or against base_url when
   it is not NULL (Linux text/html has no header). Returns how many were found. */
size_t wg_extract_urls_html(const char *html, size_t len, const char *base_url, wg_url_cb cb, void *ctx);

/* Resolves a relative reference against an absolute base URL (RFC 3986 style). Returns a new string,
   or NULL when the result is not a supported URL. */
char *wg_url_resolve(const char *base, const char *ref);

/* ------------------------------------------------------------------ status texts (as in the original) */

/* "Queued", "Downloading", "Paused", "Complete", "Failed" or "Failed (404)". */
void wg_text_status(char *out, size_t outsz, wg_status st, const wg_progress *p);
/* "1,234K/5,678K; 22%" or "5,678K; 100%"; returns -1 (out untouched) while the size is unknown. */
int wg_text_progress(char *out, size_t outsz, const wg_progress *p);
/* Speed in K/sec, or -1 until a second has passed. */
int64_t wg_speed(const wg_progress *p);
/* Seconds left, or -1 when unknown. */
int64_t wg_time_left(const wg_progress *p, int64_t speed);
/* "123K/sec; 1:05" while downloading, otherwise "". */
void wg_text_speed(char *out, size_t outsz, wg_status st, int64_t speed, int64_t time_left);
/* "2 downloading; 3 queued; 456K/sec total" (tray tooltip and status bar). */
void wg_text_tooltip(char *out, size_t outsz, int downloading, int queued, int64_t total_speed);
/* "1,234,567" */
void wg_commafy(char *out, size_t outsz, int64_t val);

/* ------------------------------------------------------------------ queue files (.wgq) */

typedef struct {
  char *url;
  int status; /* wg_status */
  int flags;  /* WG_FLAG_* */
} wg_queue_item;

typedef void (*wg_queue_cb)(const wg_queue_item *item, void *ctx);

typedef struct {
  int loaded;    /* items passed to the callback */
  int skipped;   /* records dropped because the URL was unsupported */
  int truncated; /* 1 if the file ended in the middle of a record or a record was malformed */
} wg_queue_report;

#define WG_QUEUE_VERSION 12
#define WG_QUEUE_MAX_FILE (64 * 1024 * 1024)

enum { WG_OK = 0, WG_ERR_IO = -1, WG_ERR_FORMAT = -2, WG_ERR_VERSION = -3, WG_ERR_TOOBIG = -4 };

/* Parses .wgq data (the original binary format, version <= 12). Files are untrusted: lengths are
   bounds-checked, unsupported URLs are skipped, unknown statuses become Queued, and Downloading
   becomes Queued so loaded items obey the download limit. */
int wg_queue_parse(const unsigned char *data, size_t len, wg_queue_cb cb, void *ctx, wg_queue_report *rep);
int wg_queue_load(const char *path, wg_queue_cb cb, void *ctx, wg_queue_report *rep);
/* Writes a .wgq file that the original WackGet 1.2.x can also read (written atomically). */
int wg_queue_save(const char *path, const wg_queue_item *items, size_t n);

/* ------------------------------------------------------------------ files and folders */

/* The local file name for a URL, like wget: the last path segment, percent-decoded, made safe on
   every OS (no separators, reserved names or control characters). "index.html" when empty. */
char *wg_fs_name_from_url(const char *url);
/* dir + separator + name (new string). */
char *wg_fs_join(const char *dir, const char *name);
int wg_fs_exists(const char *path);
/* Size and modification time (Unix seconds) of a file; -1 if it doesn't exist. */
int wg_fs_stat(const char *path, int64_t *size, int64_t *mtime);
int wg_fs_set_mtime(const char *path, int64_t mtime);
int wg_fs_remove(const char *path); /* 0 on success or when it's already gone */
int wg_fs_mkdirs(const char *path);
/* fopen() with a UTF-8 path on every OS. */
void *wg_fs_fopen(const char *path, const char *mode);

/* The user's Downloads folder, the config folder (created on demand) and the config file path. */
char *wg_default_download_dir(void);
char *wg_config_dir(void);
char *wg_config_path(void);

/* ------------------------------------------------------------------ settings */

/* Key=value settings with the original registry value names (DownloadDir, MaxThreads, ...). */
typedef struct wg_config wg_config;
wg_config *wg_config_new(void);
void wg_config_free(wg_config *c);
int wg_config_load(wg_config *c, const char *path);  /* a missing file is not an error */
int wg_config_save(const wg_config *c, const char *path); /* atomic */
const char *wg_config_get(const wg_config *c, const char *key, const char *dfl);
int wg_config_get_int(const wg_config *c, const char *key, int dfl);
void wg_config_set(wg_config *c, const char *key, const char *val);
void wg_config_set_int(wg_config *c, const char *key, int val);

/* ------------------------------------------------------------------ downloads */

typedef struct {
  const char *url;
  const char *dir;            /* download folder */
  int resume;                 /* 1: continue a partial file; 0: like wget --timestamping */
  const char *proxy;          /* "host:port" or NULL */
  const char *proxy_user;
  const char *proxy_pass;
  long retry_wait_ms;         /* wait between retries (original --waitretry=1 = 1000) */
  int max_tries;              /* 0 = keep trying (original --tries=0) */
} wg_dl_opts;

typedef enum { WG_DL_RUNNING, WG_DL_DONE, WG_DL_FAILED, WG_DL_STOPPED } wg_dl_state;

typedef struct {
  wg_dl_state state;
  wg_progress prog;
  char name[512];   /* local file name (UTF-8) */
  char path[4096];  /* full local path */
  char message[256];/* last error in words, for logs and tooltips */
} wg_dl_info;

typedef struct wg_dl wg_dl;

int wg_global_init(void);    /* once, before any download */
void wg_global_cleanup(void);

/* Starts a download on its own thread. Never NULL. */
wg_dl *wg_dl_start(const wg_dl_opts *opts);
/* Copies the current state (cheap; meant to be called once a second like the original timer). */
void wg_dl_snapshot(wg_dl *dl, wg_dl_info *out);
/* Asks the download to stop (pause); the partial file is kept for resuming. Returns at once. */
void wg_dl_stop(wg_dl *dl);
/* Waits for the thread to end and frees everything. Call wg_dl_stop first to end it early. */
void wg_dl_free(wg_dl *dl);

/* Appends a completed URL to the download log (original "Log" setting). */
int wg_log_completed(const char *log_path, const char *url);

#ifdef __cplusplus
}
#endif
#endif
