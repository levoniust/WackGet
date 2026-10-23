/* wgdl: downloads URLs with the WackGet core and prints the same Status / Progress / Speed texts the
 * window shows, once a second. A test aid for the core on every platform.
 *
 *   wgdl [-d DIR] [-c] [-t TRIES] [-x PROXY] [-q QUEUE.wgq] URL...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wackget/core.h"

#ifdef _WIN32
#include <windows.h>
static void sleep_s(void) { Sleep(1000); }
#else
#include <unistd.h>
static void sleep_s(void) { sleep(1); }
#endif

typedef struct {
  char **urls;
  size_t n, cap;
} url_list;

static void add_url(url_list *l, const char *url)
{
  if(l->n==l->cap) {
    l->cap= l->cap? l->cap*2: 8;
    l->urls= wg_realloc(l->urls, l->cap*sizeof(char *));
  }
  l->urls[l->n++]= wg_strdup(url);
}

static void on_queue_item(const wg_queue_item *item, void *ctx)
{
  if(item->status!=WG_STAT_DONE) {
    add_url(ctx, item->url);
  }
}

static void usage(void)
{
  fputs("usage: wgdl [-d DIR] [-c] [-t TRIES] [-x PROXY] [-q QUEUE.wgq] URL...\n"
        "  -d DIR   download folder (default: your Downloads folder)\n"
        "  -c       continue partial files (otherwise: skip files that are up to date)\n"
        "  -t N     give up after N tries (default 0 = keep trying)\n"
        "  -x HOST:PORT  proxy\n"
        "  -q FILE  also download the unfinished items of a .wgq queue file\n", stderr);
}

static wg_status to_status(wg_dl_state st)
{
  switch(st) {
  case WG_DL_DONE: return WG_STAT_DONE;
  case WG_DL_FAILED: return WG_STAT_FAILED;
  case WG_DL_STOPPED: return WG_STAT_PAUSED;
  default: return WG_STAT_GOING;
  }
}

int main(int argc, char **argv)
{
  url_list l= {0};
  char *dir= NULL;
  const char *proxy= NULL;
  int resume= 0, tries= 0, i, running, failed= 0;
  wg_dl **dls;
  size_t k;

#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  for(i= 1; i<argc; i++) {
    if(strcmp(argv[i], "-d")==0 && i+1<argc) {
      dir= wg_strdup(argv[++i]);
    } else if(strcmp(argv[i], "-c")==0) {
      resume= 1;
    } else if(strcmp(argv[i], "-t")==0 && i+1<argc) {
      tries= atoi(argv[++i]);
    } else if(strcmp(argv[i], "-x")==0 && i+1<argc) {
      proxy= argv[++i];
    } else if(strcmp(argv[i], "-q")==0 && i+1<argc) {
      wg_queue_report rep;

      if(wg_queue_load(argv[++i], on_queue_item, &l, &rep)!=WG_OK) {
        fprintf(stderr, "wgdl: can't load queue %s\n", argv[i]);
        return 2;
      }
      fprintf(stderr, "queue: %d items, %d skipped%s\n", rep.loaded, rep.skipped,
              rep.truncated? ", file damaged at the end": "");
    } else if(argv[i][0]=='-') {
      usage();
      return 2;
    } else if(wg_url_is_supported(argv[i])) {
      add_url(&l, argv[i]);
    } else {
      fprintf(stderr, "wgdl: skipping unsupported URL: %s\n", argv[i]);
    }
  }
  if(l.n==0) {
    usage();
    return 2;
  }
  if(!dir) {
    dir= wg_default_download_dir();
  }
  if(wg_global_init()!=WG_OK) {
    fputs("wgdl: libcurl failed to initialise\n", stderr);
    return 2;
  }
  dls= wg_malloc(l.n*sizeof(wg_dl *));
  for(k= 0; k<l.n; k++) {
    wg_dl_opts o;

    memset(&o, 0, sizeof(o));
    o.url= l.urls[k];
    o.dir= dir;
    o.resume= resume;
    o.proxy= proxy;
    o.max_tries= tries;
    dls[k]= wg_dl_start(&o);
  }
  do {
    int downloading= 0;
    int64_t total_speed= 0;
    char tip[128];

    sleep_s();
    running= 0;
    for(k= 0; k<l.n; k++) {
      wg_dl_info in;
      char st[64], pr[96], sp[64];
      wg_status s;
      int64_t speed;

      wg_dl_snapshot(dls[k], &in);
      s= to_status(in.state);
      wg_text_status(st, sizeof(st), s, &in.prog);
      if(wg_text_progress(pr, sizeof(pr), &in.prog)<0) {
        pr[0]= '\0';
      }
      speed= wg_speed(&in.prog);
      wg_text_speed(sp, sizeof(sp), s, speed, wg_time_left(&in.prog, speed));
      printf("%-40.40s  %-14s  %-26s  %s\n", in.name, st, pr, sp);
      if(in.state==WG_DL_RUNNING) {
        running++;
        downloading++;
        total_speed+= speed>0? speed: 0;
      }
    }
    wg_text_tooltip(tip, sizeof(tip), downloading, 0, total_speed);
    printf("-- %s\n", tip);
    fflush(stdout);
  } while(running>0);

  for(k= 0; k<l.n; k++) {
    wg_dl_info in;

    wg_dl_snapshot(dls[k], &in);
    if(in.state!=WG_DL_DONE) {
      failed++;
      fprintf(stderr, "failed: %s (%s)\n", l.urls[k], in.message);
    }
    wg_dl_free(dls[k]);
    wg_free(l.urls[k]);
  }
  wg_free(dls);
  wg_free(l.urls);
  wg_free(dir);
  wg_global_cleanup();
  return failed? 1: 0;
}
