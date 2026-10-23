/* Settings file: "Key=Value" lines, using the 1.2.x registry value names. Replaces the
   HKCU\Software\Millweed\WackGet registry keys so both OSes share one format. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

typedef struct {
  char *key, *val;
} pair_t;

struct wg_config {
  pair_t *p;
  size_t n, cap;
};

wg_config *wg_config_new(void)
{
  wg_config *c= wg_malloc(sizeof(*c));

  memset(c, 0, sizeof(*c));
  return c;
}

void wg_config_free(wg_config *c)
{
  size_t i;

  if(!c) {
    return;
  }
  for(i= 0; i<c->n; i++) {
    wg_free(c->p[i].key);
    wg_free(c->p[i].val);
  }
  wg_free(c->p);
  wg_free(c);
}

static int key_eq(const char *a, const char *b)
{
  while(*a && *b && wg_lower(*a)==wg_lower(*b)) {
    a++;
    b++;
  }
  return *a==*b;
}

static pair_t *find(const wg_config *c, const char *key)
{
  size_t i;

  for(i= 0; i<c->n; i++) {
    if(key_eq(c->p[i].key, key)) {
      return &c->p[i];
    }
  }
  return NULL;
}

/* Keys are letters, digits and '_'; values are one line. */
static char *clean(const char *s, int is_key)
{
  char *d= wg_strdup(s), *p;

  for(p= d; *p; p++) {
    if(*p=='\r' || *p=='\n' || (is_key && (*p=='=' || *p=='[' || *p==']' || *p=='#' || *p==';'))) {
      *p= is_key? '_': ' ';
    }
  }
  return d;
}

void wg_config_set(wg_config *c, const char *key, const char *val)
{
  pair_t *e= find(c, key);

  if(e) {
    wg_free(e->val);
    e->val= clean(val, 0);
    return;
  }
  if(c->n==c->cap) {
    c->cap= c->cap? c->cap*2: 32;
    c->p= wg_realloc(c->p, c->cap*sizeof(pair_t));
  }
  c->p[c->n].key= clean(key, 1);
  c->p[c->n].val= clean(val, 0);
  c->n++;
}

void wg_config_set_int(wg_config *c, const char *key, int val)
{
  char buf[24];

  snprintf(buf, sizeof(buf), "%d", val);
  wg_config_set(c, key, buf);
}

const char *wg_config_get(const wg_config *c, const char *key, const char *dfl)
{
  pair_t *e= find(c, key);

  return e? e->val: dfl;
}

int wg_config_get_int(const wg_config *c, const char *key, int dfl)
{
  const char *v= wg_config_get(c, key, NULL);
  char *end;
  long l;

  if(!v || !*v) {
    return dfl;
  }
  l= strtol(v, &end, 10);
  if(*end || l<-2147483647L-1 || l>2147483647L) {
    return dfl;
  }
  return (int)l;
}

int wg_config_load(wg_config *c, const char *path)
{
  FILE *f= wg_fs_fopen(path, "rb");
  char line[8192];

  if(!f) {
    return WG_OK; /* first run */
  }
  while(fgets(line, sizeof(line), f)) {
    char *eq, *k, *v, *e;
    size_t n= strlen(line);

    if(n==sizeof(line)-1 && line[n-1]!='\n') { /* overlong line: skip the rest of it */
      int ch;

      while((ch= fgetc(f))!=EOF && ch!='\n');
      continue;
    }
    while(n>0 && (line[n-1]=='\n' || line[n-1]=='\r')) {
      line[--n]= '\0';
    }
    k= line;
    if(n>=3 && (unsigned char)k[0]==0xef && (unsigned char)k[1]==0xbb && (unsigned char)k[2]==0xbf) {
      k+= 3; /* UTF-8 BOM (Notepad) */
    }
    while(*k==' ' || *k=='\t') {
      k++;
    }
    if(*k=='\0' || *k=='#' || *k==';' || *k=='[' || !(eq= strchr(k, '='))) {
      continue;
    }
    *eq= '\0';
    v= eq+1;
    for(e= eq; e>k && (e[-1]==' ' || e[-1]=='\t'); e--) {
      e[-1]= '\0';
    }
    while(*v==' ' || *v=='\t') {
      v++;
    }
    for(e= v+strlen(v); e>v && (e[-1]==' ' || e[-1]=='\t'); e--) {
      e[-1]= '\0';
    }
    if(*k) {
      wg_config_set(c, k, v);
    }
  }
  fclose(f);
  return WG_OK;
}

int wg_replace_file(const char *tmp, const char *path);

int wg_config_save(const wg_config *c, const char *path)
{
  char *tmp= wg_malloc(strlen(path)+5);
  FILE *f;
  size_t i;
  int ok= 1;

  sprintf(tmp, "%s.tmp", path);
  if(!(f= wg_fs_fopen(tmp, "wb"))) {
    wg_free(tmp);
    return WG_ERR_IO;
  }
  ok&= fputs("[WackGet]\n", f)>=0;
  for(i= 0; i<c->n && ok; i++) {
    ok&= fprintf(f, "%s=%s\n", c->p[i].key, c->p[i].val)>=0;
  }
  ok&= fclose(f)==0;
  if(!ok || wg_replace_file(tmp, path)!=0) {
    wg_fs_remove(tmp);
    wg_free(tmp);
    return WG_ERR_IO;
  }
  wg_free(tmp);
  return WG_OK;
}

int wg_log_completed(const char *log_path, const char *url)
{
  FILE *f= wg_fs_fopen(log_path, "ab");
  int ok;

  if(!f) {
    return WG_ERR_IO;
  }
  ok= fprintf(f, "%s\n", url)>=0;
  ok&= fclose(f)==0;
  return ok? WG_OK: WG_ERR_IO;
}
