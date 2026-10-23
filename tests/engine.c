/* Download engine tests; run by run_engine.py, which starts the test server.
   Usage: wg_engine PORT TMPDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wackget/core.h"

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void sleep_ms(int ms)
{
  struct timespec ts= {ms/1000, (ms%1000)*1000000L};

  nanosleep(&ts, NULL);
}
#endif

#define SIZE 200000
#define SLOW_SIZE 2000000
#define MTIME 1577836800

static int failures, checks;
static char base[64];
static const char *dir;

#define CHECK(cond) do { checks++; if(!(cond)) { failures++; \
  fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while(0)

static char *url_of(const char *path)
{
  char *u= wg_malloc(strlen(base)+strlen(path)+1);

  sprintf(u, "%s%s", base, path);
  return u;
}

static char *local(const char *name)
{
  return wg_fs_join(dir, name);
}

/* Downloads path and waits for it to finish (60 s at most). */
static wg_dl_info run(const char *path, int resume, int max_tries)
{
  wg_dl_opts o;
  wg_dl *dl;
  wg_dl_info in;
  char *u= url_of(path);
  int waited;

  memset(&in, 0, sizeof(in));
  memset(&o, 0, sizeof(o));
  o.url= u;
  o.dir= dir;
  o.resume= resume;
  o.retry_wait_ms= 100;
  o.max_tries= max_tries;
  dl= wg_dl_start(&o);
  for(waited= 0; waited<60000; waited+= 20) {
    wg_dl_snapshot(dl, &in);
    if(in.state!=WG_DL_RUNNING) {
      break;
    }
    sleep_ms(20);
  }
  wg_dl_stop(dl);
  wg_dl_free(dl);
  wg_free(u);
  return in;
}

/* 1 if the file holds the first n bytes of the server's pattern (i % 251). */
static int content_ok(const char *name, long n)
{
  char *p= local(name);
  FILE *f= wg_fs_fopen(p, "rb");
  long i;
  int c, ok= 1;

  wg_free(p);
  if(!f) {
    return 0;
  }
  for(i= 0; (c= fgetc(f))!=EOF; i++) {
    if(c!=(int)(i%251)) {
      ok= 0;
      break;
    }
  }
  fclose(f);
  return ok && i==n;
}

static void write_prefix(const char *name, long n)
{
  char *p= local(name);
  FILE *f= wg_fs_fopen(p, "wb");
  long i;

  for(i= 0; i<n; i++) {
    fputc((int)(i%251), f);
  }
  fclose(f);
  wg_free(p);
}

static int64_t file_size(const char *name)
{
  char *p= local(name);
  int64_t size= -1;

  wg_fs_stat(p, &size, NULL);
  wg_free(p);
  return size;
}

static int64_t file_mtime(const char *name)
{
  char *p= local(name);
  int64_t m= -1;

  wg_fs_stat(p, NULL, &m);
  wg_free(p);
  return m;
}

static void set_mtime(const char *name, int64_t t)
{
  char *p= local(name);

  wg_fs_set_mtime(p, t);
  wg_free(p);
}

static void test_basic_and_timestamping(void)
{
  wg_dl_info in= run("/file.bin", 0, 0);

  CHECK(in.state==WG_DL_DONE);
  CHECK(strcmp(in.name, "file.bin")==0);
  CHECK(in.prog.total==SIZE && in.prog.cur==SIZE && in.prog.start==0);
  CHECK(content_ok("file.bin", SIZE));
  CHECK(file_mtime("file.bin")==MTIME); /* server date kept, like wget */

  in= run("/file.bin", 0, 0); /* same date and size: not downloaded again (wget -N) */
  CHECK(in.state==WG_DL_DONE);
  CHECK(strstr(in.message, "up to date")!=NULL);
  CHECK(in.prog.cur==SIZE);

  set_mtime("file.bin", MTIME-86400); /* local copy older: fetch again */
  in= run("/file.bin", 0, 0);
  CHECK(in.state==WG_DL_DONE && strstr(in.message, "up to date")==NULL);
  CHECK(content_ok("file.bin", SIZE) && file_mtime("file.bin")==MTIME);

  write_prefix("file.bin", 1000); /* leftover partial with the server's date: not "up to date" */
  set_mtime("file.bin", MTIME);
  in= run("/file.bin", 0, 0);
  CHECK(in.state==WG_DL_DONE && content_ok("file.bin", SIZE));
}

static void test_resume(void)
{
  wg_dl_info in;

  write_prefix("file.bin", 50000);
  in= run("/file.bin", 1, 0);
  CHECK(in.state==WG_DL_DONE);
  CHECK(in.prog.start==50000 && in.prog.total==SIZE);
  CHECK(content_ok("file.bin", SIZE));

  in= run("/file.bin", 1, 0); /* already complete: server answers 416 */
  CHECK(in.state==WG_DL_DONE && content_ok("file.bin", SIZE));

  write_prefix("norange.bin", 50000); /* server ignores ranges: start over */
  in= run("/norange.bin", 1, 0);
  CHECK(in.state==WG_DL_DONE);
  CHECK(content_ok("norange.bin", SIZE));
}

static void test_redirects_and_errors(void)
{
  wg_dl_info in;

  in= run("/redirect", 0, 0);
  CHECK(in.state==WG_DL_DONE && content_ok("redirect", SIZE));

  in= run("/redirect-file", 0, 0); /* never follow a redirect to a local file */
  CHECK(in.state==WG_DL_FAILED);
  CHECK(file_size("redirect-file")<=0);

  in= run("/missing", 0, 0);
  CHECK(in.state==WG_DL_FAILED && in.prog.error==404);
  CHECK(file_size("missing")==-1); /* the error page isn't saved as the file */
  {
    char st[64];

    wg_text_status(st, sizeof(st), WG_STAT_FAILED, &in.prog);
    CHECK(strcmp(st, "Failed (404)")==0);
  }

  in= run("/flaky.bin?id=1", 0, 0); /* 503 once, then fine */
  CHECK(in.state==WG_DL_DONE && content_ok("flaky.bin", SIZE));

  in= run("/always503", 0, 3);
  CHECK(in.state==WG_DL_FAILED && in.prog.error==503);

  in= run("/drop.bin?id=1", 0, 0); /* connection dropped halfway: resumes */
  CHECK(in.state==WG_DL_DONE && content_ok("drop.bin", SIZE));

  {
    wg_dl_opts o;
    wg_dl *dl;

    memset(&o, 0, sizeof(o));
    o.url= "file:///etc/passwd";
    o.dir= dir;
    dl= wg_dl_start(&o);
    wg_dl_snapshot(dl, &in);
    CHECK(in.state==WG_DL_FAILED);
    wg_dl_free(dl);
  }
}

static void test_stop_and_resume(void)
{
  wg_dl_opts o;
  wg_dl *dl;
  wg_dl_info in;
  char *u= url_of("/slow.bin");
  int waited;
  int64_t partial;

  memset(&in, 0, sizeof(in));
  memset(&o, 0, sizeof(o));
  o.url= u;
  o.dir= dir;
  o.retry_wait_ms= 100;
  dl= wg_dl_start(&o);
  for(waited= 0; waited<20000; waited+= 20) {
    wg_dl_snapshot(dl, &in);
    if(in.prog.cur>200000 || in.state!=WG_DL_RUNNING) {
      break;
    }
    sleep_ms(20);
  }
  CHECK(in.state==WG_DL_RUNNING);
  CHECK(in.prog.total==SLOW_SIZE);
  wg_dl_stop(dl); /* Pause */
  for(waited= 0; waited<5000; waited+= 20) {
    wg_dl_snapshot(dl, &in);
    if(in.state!=WG_DL_RUNNING) {
      break;
    }
    sleep_ms(20);
  }
  CHECK(in.state==WG_DL_STOPPED);
  wg_dl_free(dl);
  wg_free(u);
  partial= file_size("slow.bin");
  CHECK(partial>0 && partial<SLOW_SIZE);
  CHECK(content_ok("slow.bin", (long)partial));

  in= run("/slow.bin", 1, 0); /* Resume */
  CHECK(in.state==WG_DL_DONE);
  CHECK(in.prog.start==partial);
  CHECK(content_ok("slow.bin", SLOW_SIZE));
}

static void test_misc(void)
{
  wg_dl_info in;
  char *p, want[80], got[80] = {0};
  FILE *f;

  in= run("/referer", 0, 0); /* scheme + host, like 1.2.x's --referer */
  CHECK(in.state==WG_DL_DONE);
  p= local("referer");
  if((f= wg_fs_fopen(p, "rb"))) {
    size_t n= fread(got, 1, sizeof(got)-1, f);

    got[n]= '\0';
    fclose(f);
  }
  wg_free(p);
  snprintf(want, sizeof(want), "%s/", base);
  CHECK(strcmp(got, want)==0);

  in= run("/chunked.bin", 0, 0); /* size unknown until the end */
  CHECK(in.state==WG_DL_DONE && in.prog.total==SIZE && content_ok("chunked.bin", SIZE));

  in= run("/empty", 0, 0);
  CHECK(in.state==WG_DL_DONE && file_size("empty")==0);
}

int main(int argc, char **argv)
{
  if(argc<3) {
    fputs("usage: wg_engine PORT TMPDIR\n", stderr);
    return 2;
  }
  snprintf(base, sizeof(base), "http://127.0.0.1:%s", argv[1]);
  dir= argv[2];
  if(wg_global_init()!=WG_OK) {
    return 2;
  }
  test_basic_and_timestamping();
  test_resume();
  test_redirects_and_errors();
  test_stop_and_resume();
  test_misc();
  wg_global_cleanup();
  printf("%d checks, %d failures\n", checks, failures);
  return failures? 1: 0;
}
