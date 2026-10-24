/* Unit tests for the WackGet core. No framework: CHECK prints failures and the exit code counts them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wackget/core.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if(!(cond)) { failures++; \
  fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while(0)
#define CHECK_STR(got, want) do { const char *g_= (got), *w_= (want); checks++; \
  if(!(g_ && w_ ? strcmp(g_, w_)==0 : g_==w_)) { failures++; \
  fprintf(stderr, "%s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, g_? g_: "(null)", w_? w_: "(null)"); } } while(0)

/* ---------------------------------------------------------------- URLs */

static void test_supported(void)
{
  char *longurl;

  CHECK(wg_url_is_supported("http://a"));
  CHECK(wg_url_is_supported("HTTPS://example.com/x?y=1#z"));
  CHECK(wg_url_is_supported("ftp://ftp.example.com/pub/file.tar.gz"));
  CHECK(wg_url_is_supported("http://user:pw@host:8080/p"));
  CHECK(!wg_url_is_supported("h://x"));   /* 1.2.x accepted these */
  CHECK(!wg_url_is_supported("ht://x"));
  CHECK(!wg_url_is_supported("://x"));
  CHECK(!wg_url_is_supported("file:///etc/passwd"));
  CHECK(!wg_url_is_supported("javascript:alert(1)"));
  CHECK(!wg_url_is_supported("http://"));
  CHECK(!wg_url_is_supported("http:///path"));
  CHECK(!wg_url_is_supported("http://a b"));
  CHECK(!wg_url_is_supported("http://a/x -O /etc/x")); /* the 1.2.x command-line injection */
  CHECK(!wg_url_is_supported("http://a/\"x"));
  CHECK(!wg_url_is_supported("http://a/\n"));
  CHECK(!wg_url_is_supported(""));
  CHECK(!wg_url_is_supported(NULL));
  longurl= malloc(WG_URL_MAX+2);
  memcpy(longurl, "http://a/", 9);
  memset(longurl+9, 'x', WG_URL_MAX-8);
  longurl[WG_URL_MAX+1]= '\0';
  CHECK(!wg_url_is_supported(longurl));
  longurl[WG_URL_MAX]= '\0';
  CHECK(wg_url_is_supported(longurl));
  free(longurl);
}

static void test_referer(void)
{
  char *r;

  CHECK_STR(r= wg_url_referer("http://user:pw@host.example:8080/p?q"), "http://host.example:8080/");
  wg_free(r);
  CHECK_STR(r= wg_url_referer("https://h"), "https://h/");
  wg_free(r);
  CHECK(wg_url_referer("file:///x")==NULL);
}

static void check_resolve(const char *ref, const char *want, int line)
{
  char *got= wg_url_resolve("http://a/b/c/d;p?q", ref);

  checks++;
  if(!(got && want? strcmp(got, want)==0: got==want)) {
    failures++;
    fprintf(stderr, "%s:%d: resolve(\"%s\") = \"%s\", want \"%s\"\n", __FILE__, line, ref,
            got? got: "(null)", want? want: "(null)");
  }
  wg_free(got);
}
#define RESOLVE(ref, want) check_resolve(ref, want, __LINE__)

static void test_resolve(void)
{
  /* RFC 3986 section 5.4 examples (fragments are dropped: they never reach the server) */
  RESOLVE("g", "http://a/b/c/g");
  RESOLVE("./g", "http://a/b/c/g");
  RESOLVE("g/", "http://a/b/c/g/");
  RESOLVE("/g", "http://a/g");
  RESOLVE("//g", "http://g");
  RESOLVE("?y", "http://a/b/c/d;p?y");
  RESOLVE("g?y", "http://a/b/c/g?y");
  RESOLVE("#s", "http://a/b/c/d;p?q");
  RESOLVE("g#s", "http://a/b/c/g");
  RESOLVE(";x", "http://a/b/c/;x");
  RESOLVE("", "http://a/b/c/d;p?q");
  RESOLVE(".", "http://a/b/c/");
  RESOLVE("./", "http://a/b/c/");
  RESOLVE("..", "http://a/b/");
  RESOLVE("../", "http://a/b/");
  RESOLVE("../g", "http://a/b/g");
  RESOLVE("../..", "http://a/");
  RESOLVE("../../g", "http://a/g");
  RESOLVE("../../../g", "http://a/g");
  RESOLVE("/./g", "http://a/g");
  RESOLVE("/../g", "http://a/g");
  RESOLVE("g.", "http://a/b/c/g.");
  RESOLVE("..g", "http://a/b/c/..g");
  RESOLVE("./g/.", "http://a/b/c/g/");
  RESOLVE("g/./h", "http://a/b/c/g/h");
  RESOLVE("g/../h", "http://a/b/c/h");
  RESOLVE("https://other/x", "https://other/x");
  RESOLVE("mailto:x@y", NULL);
  RESOLVE("file:///etc/passwd", NULL);
}

typedef struct {
  char got[16][256];
  int n;
} url_bag;

static void collect(const char *url, void *ctx)
{
  url_bag *b= ctx;

  if(b->n<16) {
    snprintf(b->got[b->n++], 256, "%s", url);
  }
}

static void test_extract_text(void)
{
  url_bag b= {{{0}}, 0};
  const char *t= "get http://a/x.zip, then HTTPS://b/y.iso. (see http://w/Foo_(bar)) and (http://p/q) "
                 "again http://a/x.zip ftp://f/g 'http://s/q' \"http://d/q\"<http://lt/q> xhttp://m/n "
                 "h://bad ://bad file:///etc/passwd";
  size_t n= wg_extract_urls_text(t, strlen(t), collect, &b);

  CHECK(n==9);
  CHECK_STR(b.got[0], "http://a/x.zip");
  CHECK_STR(b.got[1], "HTTPS://b/y.iso");
  CHECK_STR(b.got[2], "http://w/Foo_(bar)");
  CHECK_STR(b.got[3], "http://p/q");
  CHECK_STR(b.got[4], "ftp://f/g"); /* duplicate http://a/x.zip dropped */
  CHECK_STR(b.got[5], "http://s/q");
  CHECK_STR(b.got[6], "http://d/q");
  CHECK_STR(b.got[7], "http://lt/q");
  CHECK_STR(b.got[8], "http://m/n");
}

static void test_extract_html(void)
{
  url_bag b= {{{0}}, 0};
  const char *cf=
    "Version:0.9\r\nStartHTML:0000000105\r\nEndHTML:0000000999\r\n"
    "SourceURL:http://site.example/dir/page.html\r\n"
    "<html><body><!--StartFragment--><a href=\"file.zip\">x</a> <a href='/root.iso'>y</a>"
    " <img src=pic.png> <a href=\"#top\">t</a> <a href=\"javascript:void(0)\">j</a>"
    " http://bare.example/one <a HREF = \"a.zip?x=1&amp;y=2\">z</a> <a hreflang=en href=\"../up.bin\">u</a>"
    " <a href=\"mailto:me@x\">m</a><!--EndFragment--> <a href=\"after-end.zip\">no</a></body></html>";
  const char *linux_html= "<meta charset=utf-8><a href=\"rel/one.zip\">1</a><a href=\"https://abs/two\">2</a>";
  size_t n;

  n= wg_extract_urls_html(cf, strlen(cf), NULL, collect, &b);
  CHECK(n==6);
  CHECK_STR(b.got[0], "http://site.example/dir/file.zip");
  CHECK_STR(b.got[1], "http://site.example/root.iso");
  CHECK_STR(b.got[2], "http://site.example/dir/pic.png");
  CHECK_STR(b.got[3], "http://bare.example/one");
  CHECK_STR(b.got[4], "http://site.example/dir/a.zip?x=1&y=2");
  CHECK_STR(b.got[5], "http://site.example/up.bin");

  memset(&b, 0, sizeof(b)); /* no base: relative links can't be resolved, absolute ones still come through */
  n= wg_extract_urls_html(linux_html, strlen(linux_html), NULL, collect, &b);
  CHECK(n==1);
  CHECK_STR(b.got[0], "https://abs/two");

  memset(&b, 0, sizeof(b)); /* Linux text/html with a known base */
  n= wg_extract_urls_html(linux_html, strlen(linux_html), "http://host/a/b.html", collect, &b);
  CHECK(n==2);
  CHECK_STR(b.got[0], "http://host/a/rel/one.zip");
}

/* ---------------------------------------------------------------- texts */

static void test_texts(void)
{
  char out[128];
  wg_progress p;

  wg_commafy(out, sizeof(out), 0);
  CHECK_STR(out, "0");
  wg_commafy(out, sizeof(out), 999);
  CHECK_STR(out, "999");
  wg_commafy(out, sizeof(out), 1000);
  CHECK_STR(out, "1,000");
  wg_commafy(out, sizeof(out), 1234567);
  CHECK_STR(out, "1,234,567");
  wg_commafy(out, sizeof(out), -1234);
  CHECK_STR(out, "-1,234");
  wg_commafy(out, sizeof(out), 9007199254740993LL);
  CHECK_STR(out, "9,007,199,254,740,993");

  memset(&p, 0, sizeof(p));
  wg_text_status(out, sizeof(out), WG_STAT_QUEUED, &p);
  CHECK_STR(out, "Queued");
  wg_text_status(out, sizeof(out), WG_STAT_GOING, &p);
  CHECK_STR(out, "Downloading");
  wg_text_status(out, sizeof(out), WG_STAT_FAILED, &p);
  CHECK_STR(out, "Failed");
  p.error= 404;
  wg_text_status(out, sizeof(out), WG_STAT_FAILED, &p);
  CHECK_STR(out, "Failed (404)");
  wg_text_status(out, sizeof(out), WG_STAT_DONE, &p);
  CHECK_STR(out, "Complete");

  memset(&p, 0, sizeof(p));
  strcpy(out, "untouched");
  CHECK(wg_text_progress(out, sizeof(out), &p)==-1);
  CHECK_STR(out, "untouched");
  p.total= 5*1024*1024;
  p.cur= 1234*1024;
  CHECK(wg_text_progress(out, sizeof(out), &p)==0);
  CHECK_STR(out, "1,234K/5,120K; 24%");
  p.cur= p.total;
  wg_text_progress(out, sizeof(out), &p);
  CHECK_STR(out, "5,120K; 100%");
  p.total= 6LL*1024*1024*1024; /* > 4 GB (1.2.x printed these with %ld) */
  p.cur= 3LL*1024*1024*1024;
  wg_text_progress(out, sizeof(out), &p);
  CHECK_STR(out, "3,145,728K/6,291,456K; 50%");

  memset(&p, 0, sizeof(p));
  p.elapsed= 999;
  CHECK(wg_speed(&p)==-1);
  p.elapsed= 2000;
  p.start= 1024;
  p.cur= 1024+4096*1024;
  p.total= p.cur+2048*1024*65;
  CHECK(wg_speed(&p)==2048);
  CHECK(wg_time_left(&p, 2048)==65);
  CHECK(wg_time_left(&p, 0)==-1);
  wg_text_speed(out, sizeof(out), WG_STAT_GOING, 2048, 65);
  CHECK_STR(out, "2,048K/sec; 1:05");
  wg_text_speed(out, sizeof(out), WG_STAT_GOING, 3, -1);
  CHECK_STR(out, "3K/sec; ?");
  wg_text_speed(out, sizeof(out), WG_STAT_PAUSED, 2048, 65);
  CHECK_STR(out, "");
  wg_text_tooltip(out, sizeof(out), 2, 3, 1456);
  CHECK_STR(out, "2 downloading; 3 queued; 1,456K/sec total");
}

/* ---------------------------------------------------------------- queue files */

typedef struct {
  wg_queue_item items[8];
  int n;
} item_bag;

static void collect_item(const wg_queue_item *it, void *ctx)
{
  item_bag *b= ctx;

  if(b->n<8) {
    b->items[b->n]= *it;
    b->items[b->n].url= wg_strdup(it->url);
    b->n++;
  }
}

static void free_bag(item_bag *b)
{
  int i;

  for(i= 0; i<b->n; i++) {
    wg_free(b->items[i].url);
  }
  b->n= 0;
}

static void put32(unsigned char *p, int v)
{
  unsigned u= (unsigned)v;

  p[0]= (unsigned char)u;
  p[1]= (unsigned char)(u>>8);
  p[2]= (unsigned char)(u>>16);
  p[3]= (unsigned char)(u>>24);
}

/* One record in the 1.2.x layout */
static size_t rec(unsigned char *p, const char *url, int ulen, int status, int flags)
{
  put32(p, ulen);
  memcpy(p+4, url, strlen(url));
  put32(p+4+strlen(url), status);
  put32(p+8+strlen(url), flags);
  return 12+strlen(url);
}

static void test_queue(void)
{
  unsigned char d[512];
  size_t n;
  item_bag b= {{{0}}, 0};
  wg_queue_report rep;
  wg_queue_item save[3]= {
    {"http://a/one.zip", WG_STAT_DONE, 0},
    {"https://b/two.iso", WG_STAT_PAUSED, WG_FLAG_DELETE},
    {"ftp://c/three.tar", WG_STAT_GOING, 0xf0},
  };
  const char *path= "unit-test.wgq";

  /* 1.2.x file built by hand */
  put32(d, 12);
  n= 4;
  n+= rec(d+n, "http://x/a.zip", 14, WG_STAT_PAUSED, 1);
  n+= rec(d+n, "http://x/b.zip", 14, WG_STAT_GOING, 0);
  n+= rec(d+n, "http://x/c.zip", 14, 99, 0xff);
  CHECK(wg_queue_parse(d, n, collect_item, &b, &rep)==WG_OK);
  CHECK(rep.loaded==3 && rep.skipped==0 && !rep.truncated);
  CHECK(b.n==3);
  CHECK_STR(b.items[0].url, "http://x/a.zip");
  CHECK(b.items[0].status==WG_STAT_PAUSED && b.items[0].flags==WG_FLAG_DELETE);
  CHECK(b.items[1].status==WG_STAT_QUEUED); /* Downloading -> Queued */
  CHECK(b.items[2].status==WG_STAT_QUEUED && b.items[2].flags==WG_FLAG_DELETE); /* bad status, masked flags */
  free_bag(&b);

  /* hostile records (security review S1/S2) */
  put32(d, 12);
  n= 4;
  n+= rec(d+n, "http://x/a -O /tmp/evil", 23, 0, 0); /* argument injection attempt */
  n+= rec(d+n, "file:///etc/passwd", 18, 0, 0);
  n+= rec(d+n, "http://ok/z", 11, 0, 0);
  CHECK(wg_queue_parse(d, n, collect_item, &b, &rep)==WG_OK);
  CHECK(rep.loaded==1 && rep.skipped==2);
  CHECK(b.n==1);
  free_bag(&b);

  put32(d, 12); /* NUL inside a URL */
  put32(d+4, 12);
  memcpy(d+8, "http://x/a\0b", 12);
  put32(d+20, 0);
  put32(d+24, 0);
  n= 28;
  CHECK(wg_queue_parse(d, n, collect_item, &b, &rep)==WG_OK && rep.skipped==1 && b.n==0);

  put32(d, 12);
  put32(d+4, -1); /* negative length: heap overflow in 1.2.x */
  memset(d+8, 'A', 64);
  CHECK(wg_queue_parse(d, 72, collect_item, &b, &rep)==WG_OK && rep.truncated && b.n==0);
  put32(d+4, 0x7fffffff); /* huge length */
  CHECK(wg_queue_parse(d, 72, collect_item, &b, &rep)==WG_OK && rep.truncated && b.n==0);
  put32(d+4, 0);
  CHECK(wg_queue_parse(d, 72, collect_item, &b, &rep)==WG_OK && rep.truncated && b.n==0);

  put32(d, 12); /* record cut short */
  n= 4+rec(d+4, "http://x/a.zip", 14, 0, 0);
  CHECK(wg_queue_parse(d, n-3, collect_item, &b, &rep)==WG_OK && rep.truncated && b.n==0);

  put32(d, 13);
  CHECK(wg_queue_parse(d, 4, collect_item, &b, &rep)==WG_ERR_VERSION);
  put32(d, 0);
  CHECK(wg_queue_parse(d, 4, collect_item, &b, &rep)==WG_ERR_VERSION);
  CHECK(wg_queue_parse(d, 3, collect_item, &b, &rep)==WG_ERR_FORMAT);

  /* save + load round trip */
  CHECK(wg_queue_save(path, save, 3)==WG_OK);
  CHECK(wg_queue_load(path, collect_item, &b, &rep)==WG_OK);
  CHECK(b.n==3 && rep.loaded==3);
  CHECK_STR(b.items[1].url, "https://b/two.iso");
  CHECK(b.items[0].status==WG_STAT_DONE && b.items[1].flags==WG_FLAG_DELETE);
  CHECK(b.items[2].status==WG_STAT_QUEUED && b.items[2].flags==0);
  free_bag(&b);
  CHECK(wg_fs_remove(path)==0);
  CHECK(wg_queue_load("does-not-exist.wgq", collect_item, &b, &rep)==WG_ERR_IO);
}

/* ---------------------------------------------------------------- file names */

static void check_name(const char *url, const char *want, int line)
{
  char *got= wg_fs_name_from_url(url);

  checks++;
  if(strcmp(got, want)!=0) {
    failures++;
    fprintf(stderr, "%s:%d: name(\"%s\") = \"%s\", want \"%s\"\n", __FILE__, line, url, got, want);
  }
  wg_free(got);
}
#define NAME(url, want) check_name(url, want, __LINE__)

static void test_names(void)
{
  char url[400], *got;

  NAME("http://h/dir/file.zip?x=1#f", "file.zip@x=1"); /* wget 1.10 on Windows, as 1.2.x */
  NAME("http://h/dir/file.zip#f", "file.zip");
  NAME("http://h/d.php?id=1&p=a/b", "d.php@id=1&p=a_b");
  NAME("http://h/d.php?id=a%20b", "d.php@id=a b");
  NAME("http://h/?x=1", "index.html@x=1");
  NAME("http://h/f.zip?", "f.zip");
  NAME("http://h/", "index.html");
  NAME("http://h", "index.html");
  NAME("http://h/dir/", "index.html");
  NAME("http://h/a%20b.txt", "a b.txt");
  NAME("http://h/caf%C3%A9.txt", "café.txt");
  NAME("http://h/%FF.bin", "_.bin");
  NAME("http://h/%2e%2e", "index.html");
  NAME("http://h/..", "index.html");
  NAME("http://h/..%2f..%2fetc%2fpasswd", ".._.._etc_passwd");
  NAME("http://h/a%5cb", "a_b");
  NAME("http://h/x%00y", "x_y");
  NAME("http://h/a:b*c%3F%22%3C%3E%7C", "a_b_c_____");
  NAME("http://h/CON.txt", "_CON.txt");
  NAME("http://h/com1", "_com1");
  NAME("http://h/lpt9.log", "_lpt9.log");
  NAME("http://h/console.txt", "console.txt");
  NAME("http://h/name.", "name");
  NAME("http://h/%20lead", "lead");
  NAME("http://h/100%", "100%");
  NAME("http://h/%zz", "%zz");
  strcpy(url, "http://h/");
  memset(url+9, 'a', 300);
  strcpy(url+309, ".zip");
  got= wg_fs_name_from_url(url);
  CHECK(strlen(got)==239); /* one byte spare for a reserved-name '_' */
  CHECK(strcmp(got+235, ".zip")==0);
  wg_free(got);
  memset(url+9, 'a', 238); /* the cut lands just after a space (found by libFuzzer) */
  strcpy(url+247, " bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  got= wg_fs_name_from_url(url);
  CHECK(strlen(got)==238 && got[237]=='a');
  wg_free(got);
}

/* ---------------------------------------------------------------- settings and files */

static void test_config_and_fs(void)
{
  wg_config *c= wg_config_new();
  const char *path= "unit-test.ini";
  FILE *f;
  int64_t size= 0, mtime= 0;
  char *j;

  CHECK(wg_config_get_int(c, "MaxThreads", 3)==3);
  wg_config_set(c, "DownloadDir", "C:\\Users\\Tim\\Downloads");
  wg_config_set_int(c, "MaxThreads", 4);
  wg_config_set(c, "Evil", "line1\nInjected=1");
  wg_config_set(c, "AlwaysOnTop", "not a number");
  CHECK(wg_config_get_int(c, "maxthreads", 0)==4); /* names are case-insensitive, like the registry */
  CHECK(wg_config_get_int(c, "AlwaysOnTop", 7)==7);
  CHECK(wg_config_save(c, path)==WG_OK);
  wg_config_free(c);

  f= fopen(path, "ab");
  fputs("# comment\n; another\n  Proxy = 1 \n", f);
  fclose(f);
  c= wg_config_new();
  CHECK(wg_config_load(c, path)==WG_OK);
  CHECK_STR(wg_config_get(c, "DownloadDir", ""), "C:\\Users\\Tim\\Downloads");
  CHECK(wg_config_get_int(c, "MaxThreads", 0)==4);
  CHECK(wg_config_get_int(c, "Proxy", 0)==1);
  CHECK(wg_config_get(c, "Injected", NULL)==NULL);
  CHECK_STR(wg_config_get(c, "Evil", ""), "line1 Injected=1");
  wg_config_free(c);
  CHECK(wg_fs_remove(path)==0);
  c= wg_config_new();
  CHECK(wg_config_load(c, "missing.ini")==WG_OK); /* first run */
  wg_config_free(c);

  j= wg_fs_join("dir", "f.txt");
#ifdef _WIN32
  CHECK_STR(j, "dir\\f.txt");
#else
  CHECK_STR(j, "dir/f.txt");
#endif
  wg_free(j);
  j= wg_fs_join("dir/", "f.txt");
  CHECK_STR(j, "dir/f.txt");
  wg_free(j);

  CHECK(wg_fs_mkdirs("unit-dir/a/b")==0);
  CHECK(wg_fs_exists("unit-dir/a/b"));
  f= wg_fs_fopen("unit-dir/a/b/café.bin", "wb");
  CHECK(f!=NULL);
  if(f) {
    fwrite("12345", 1, 5, f);
    fclose(f);
  }
  CHECK(wg_fs_set_mtime("unit-dir/a/b/café.bin", 1000000000)==0);
  CHECK(wg_fs_stat("unit-dir/a/b/café.bin", &size, &mtime)==0);
  CHECK(size==5 && mtime==1000000000);
  CHECK(wg_fs_remove("unit-dir/a/b/café.bin")==0);
  CHECK(wg_fs_remove("unit-dir/a/b/café.bin")==0); /* already gone is fine */
  CHECK(wg_fs_stat("unit-dir/a/b/café.bin", &size, &mtime)==-1);
  CHECK(wg_log_completed("unit-dir/log.txt", "http://a/b")==WG_OK);
  CHECK(wg_fs_stat("unit-dir/log.txt", &size, NULL)==0 && size==11);
  wg_fs_remove("unit-dir/log.txt");

  j= wg_default_download_dir();
  CHECK(j && *j);
  wg_free(j);
}

int main(void)
{
  test_supported();
  test_referer();
  test_resolve();
  test_extract_text();
  test_extract_html();
  test_texts();
  test_queue();
  test_names();
  test_config_and_fs();
  printf("%d checks, %d failures\n", checks, failures);
  return failures? 1: 0;
}
