/* URL checks and URL extraction from pasted or dropped text and HTML (was util.c in 1.2.x). */
#include <stdlib.h>
#include <string.h>

#include "internal.h"

static const char *const schemes[]= {"https", "http", "ftp"};

/* Length of a supported scheme at s (followed by "://"), or 0. */
static size_t scheme_len(const char *s, size_t len)
{
  size_t i;

  for(i= 0; i<sizeof(schemes)/sizeof(schemes[0]); i++) {
    size_t n= strlen(schemes[i]);

    if(len>=n+3 && wg_prefix_ci(s, len, schemes[i]) && memcmp(s+n, "://", 3)==0) {
      return n;
    }
  }
  return 0;
}

int wg_url_is_supported(const char *url)
{
  size_t len, n, i;

  if(!url || (len= strlen(url))==0 || len>WG_URL_MAX || (n= scheme_len(url, len))==0) {
    return 0;
  }
  for(i= 0; i<len; i++) {
    unsigned char c= (unsigned char)url[i];

    if(c<=0x20 || c==0x7f || c=='"' || c=='<' || c=='>' || c=='\\' || c=='`') {
      return 0;
    }
  }
  /* Needs a host: something after "://" before the path, query or fragment */
  i= n+3;
  return url[i] && url[i]!='/' && url[i]!='?' && url[i]!='#' && url[i]!='@';
}

char *wg_url_referer(const char *url)
{
  const char *auth, *end, *at;
  size_t n;
  wg_buf b= {0};

  if(!wg_url_is_supported(url)) {
    return NULL;
  }
  n= scheme_len(url, strlen(url));
  auth= url+n+3;
  end= auth+strcspn(auth, "/?#");
  for(at= end; at>auth && at[-1]!='@'; at--);
  if(at>auth) {
    auth= at; /* drop user:password@ */
  }
  if(auth==end) {
    return NULL;
  }
  wg_buf_add(&b, url, n+3);
  wg_buf_add(&b, auth, (size_t)(end-auth));
  wg_buf_addc(&b, '/');
  return b.p;
}

/* ---------------------------------------------------------------- resolving relative links */

typedef struct {
  const char *scheme, *auth, *path, *query; /* query includes '?', may be "" */
  size_t scheme_n, auth_n, path_n, query_n;
} url_parts;

static int split_url(const char *url, url_parts *u)
{
  size_t len= strlen(url);

  if((u->scheme_n= scheme_len(url, len))==0) {
    return -1;
  }
  u->scheme= url;
  u->auth= url+u->scheme_n+3;
  u->auth_n= strcspn(u->auth, "/?#");
  u->path= u->auth+u->auth_n;
  u->path_n= strcspn(u->path, "?#");
  u->query= u->path+u->path_n;
  u->query_n= (*u->query=='?')? strcspn(u->query, "#"): 0;
  return 0;
}

/* RFC 3986 5.2.4: removes "." and ".." segments from a path that starts with '/'. */
static void remove_dots(wg_buf *out, const char *path, size_t n)
{
  size_t i= 0, keep= out->len; /* never pop into what's already in out (scheme and host) */

  while(i<n) {
    size_t j= i+1, seg;

    while(j<n && path[j]!='/') {
      j++;
    }
    seg= j-i-1; /* segment after the '/' at i */
    if(seg==1 && path[i+1]=='.') {
      if(j>=n) {
        wg_buf_addc(out, '/');
      }
    } else if(seg==2 && path[i+1]=='.' && path[i+2]=='.') {
      while(out->len>keep && out->p[out->len-1]!='/') {
        out->len--;
      }
      if(out->len>keep) {
        out->len--; /* drop the '/' too */
      }
      out->p[out->len]= '\0';
      if(j>=n) {
        wg_buf_addc(out, '/');
      }
    } else {
      wg_buf_add(out, path+i, j-i);
    }
    i= j;
  }
  if(out->len==keep) {
    wg_buf_addc(out, '/');
  }
}

char *wg_url_resolve(const char *base, const char *ref)
{
  url_parts b;
  wg_buf out= {0}, merged= {0};
  size_t i, rn;
  const char *rq;

  while(*ref==' ' || *ref=='\t' || *ref=='\r' || *ref=='\n') {
    ref++;
  }
  /* Absolute? ("scheme:" before any '/', '?' or '#') */
  for(i= 0; ref[i] && ref[i]!=':' && ref[i]!='/' && ref[i]!='?' && ref[i]!='#'; i++);
  if(ref[i]==':' && i>0) {
    return wg_url_is_supported(ref)? wg_strdup(ref): NULL;
  }
  if(!base || split_url(base, &b)<0) {
    return NULL;
  }
  wg_buf_add(&out, b.scheme, b.scheme_n);
  wg_buf_adds(&out, "://");
  if(ref[0]=='/' && ref[1]=='/') { /* network-path reference: same scheme, new host */
    wg_buf_adds(&out, ref+2);
  } else {
    wg_buf_add(&out, b.auth, b.auth_n);
    rn= strcspn(ref, "?#");
    rq= ref+rn;
    if(rn==0) { /* "", "?q" or "#frag": the base path */
      wg_buf_add(&out, b.path_n? b.path: "/", b.path_n? b.path_n: 1);
      if(*rq=='?') {
        wg_buf_add(&out, rq, strcspn(rq, "#"));
      } else {
        wg_buf_add(&out, b.query, b.query_n);
      }
    } else {
      if(ref[0]=='/') {
        wg_buf_add(&merged, ref, rn);
      } else { /* relative to the base's directory */
        size_t dir= b.path_n;

        while(dir>0 && b.path[dir-1]!='/') {
          dir--;
        }
        if(dir==0) {
          wg_buf_addc(&merged, '/');
        } else {
          wg_buf_add(&merged, b.path, dir);
        }
        wg_buf_add(&merged, ref, rn);
      }
      remove_dots(&out, merged.p, merged.len);
      wg_free(merged.p);
      if(*rq=='?') {
        wg_buf_add(&out, rq, strcspn(rq, "#"));
      }
    }
  }
  if(!wg_url_is_supported(out.p)) {
    wg_free(out.p);
    return NULL;
  }
  return out.p;
}

/* ---------------------------------------------------------------- extraction */

/* Characters that end a URL in running text (the original's INVALID_URL_CHARS plus controls). */
static int ends_url(unsigned char c)
{
  return c<=0x20 || c==0x7f || c=='"' || c=='<' || c=='>' || c=='\'' || c=='`';
}

/* Remembers what one paste produced so the same link isn't queued twice. */
typedef struct {
  wg_url_cb cb;
  void *ctx;
  char **seen;
  size_t n, cap, count;
} sink_t;

static void sink_add(sink_t *s, char *url) /* takes ownership */
{
  size_t i;

  if(!url) {
    return;
  }
  if(!wg_url_is_supported(url)) {
    wg_free(url);
    return;
  }
  for(i= 0; i<s->n; i++) {
    if(strcmp(s->seen[i], url)==0) {
      wg_free(url);
      return;
    }
  }
  if(s->n==s->cap) {
    s->cap= s->cap? s->cap*2: 16;
    s->seen= wg_realloc(s->seen, s->cap*sizeof(char *));
  }
  s->seen[s->n++]= url;
  s->count++;
  if(s->cb) {
    s->cb(url, s->ctx);
  }
}

static void sink_done(sink_t *s)
{
  size_t i;

  for(i= 0; i<s->n; i++) {
    wg_free(s->seen[i]);
  }
  wg_free(s->seen);
}

/* Length of the URL starting at s (a supported scheme), minus sentence punctuation at the end. */
static size_t url_span(const char *s, size_t len)
{
  size_t n= 0, opens= 0, closes= 0, i;

  while(n<len && !ends_url((unsigned char)s[n])) {
    n++;
  }
  for(i= 0; i<n; i++) {
    opens+= (s[i]=='(');
    closes+= (s[i]==')');
  }
  while(n>0) {
    char c= s[n-1];

    if(c=='.' || c==',' || c==';' || c==':' || c=='!' || c=='?') {
      n--;
    } else if(c==')' && closes>opens) {
      n--;
      closes--;
    } else {
      break;
    }
  }
  return n;
}

/* Finds the next "scheme://" at or after text[*pos]; returns its start or -1. */
static long next_scheme(const char *text, size_t len, size_t from)
{
  size_t i;

  for(i= from; i+3<=len; i++) {
    if(text[i]==':' && i+3<=len && text[i+1]=='/' && text[i+2]=='/') {
      size_t k;

      for(k= 0; k<sizeof(schemes)/sizeof(schemes[0]); k++) {
        size_t n= strlen(schemes[k]);

        if(i>=n && wg_prefix_ci(text+i-n, len-(i-n), schemes[k])) {
          return (long)(i-n);
        }
      }
    }
  }
  return -1;
}

static void extract_text(sink_t *s, const char *text, size_t len)
{
  size_t pos= 0;
  long at;

  while((at= next_scheme(text, len, pos))>=0) {
    size_t n= url_span(text+at, len-(size_t)at);

    if(n>0 && n<=WG_URL_MAX) {
      sink_add(s, wg_strndup(text+at, n));
    }
    pos= (size_t)at+(n? n: 1);
  }
}

size_t wg_extract_urls_text(const char *text, size_t len, wg_url_cb cb, void *ctx)
{
  sink_t s= {cb, ctx, NULL, 0, 0, 0};

  extract_text(&s, text, len);
  sink_done(&s);
  return s.count;
}

static const char *find_ci(const char *h, size_t hlen, const char *needle)
{
  size_t n= strlen(needle), i;

  for(i= 0; i+n<=hlen; i++) {
    if(wg_prefix_ci(h+i, hlen-i, needle)) {
      return h+i;
    }
  }
  return NULL;
}

/* Decodes the HTML character references that show up in links (&amp;, &#38;, &#x26; ...). */
static char *decode_entities(const char *s, size_t n)
{
  wg_buf b= {0};
  size_t i= 0;

  wg_buf_add(&b, "", 0);
  while(i<n) {
    if(s[i]=='&') {
      static const struct { const char *name; char c; } named[]= {
        {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'}, {"apos;", '\''}, {"#39;", '\''},
      };
      size_t k, rest= n-i-1;
      int done= 0;

      for(k= 0; k<sizeof(named)/sizeof(named[0]); k++) {
        size_t m= strlen(named[k].name);

        if(rest>=m && memcmp(s+i+1, named[k].name, m)==0) {
          wg_buf_addc(&b, named[k].c);
          i+= m+1;
          done= 1;
          break;
        }
      }
      if(!done && rest>=3 && s[i+1]=='#') {
        size_t j= i+2;
        int hex= (s[j]=='x' || s[j]=='X');
        unsigned long cp= 0;
        int digits= 0;

        j+= hex;
        while(j<n && digits<8) {
          char c= s[j];
          int v= (c>='0' && c<='9')? c-'0':
            (hex && c>='a' && c<='f')? c-'a'+10: (hex && c>='A' && c<='F')? c-'A'+10: -1;

          if(v<0) {
            break;
          }
          cp= cp*(hex? 16: 10)+(unsigned long)v;
          digits++;
          j++;
        }
        if(digits>0 && j<n && s[j]==';') {
          wg_buf_add_utf8(&b, cp);
          i= j+1;
          done= 1;
        }
      }
      if(done) {
        continue;
      }
    }
    wg_buf_addc(&b, s[i++]);
  }
  return b.p;
}

static int skip_link(const char *v)
{
  static const char *const pfx[]= {"javascript:", "mailto:", "data:", "tel:", "about:"};
  size_t i, len= strlen(v);

  if(len==0 || v[0]=='#') {
    return 1;
  }
  for(i= 0; i<sizeof(pfx)/sizeof(pfx[0]); i++) {
    if(wg_prefix_ci(v, len, pfx[i])) {
      return 1;
    }
  }
  return 0;
}

void wg_buf_add_utf8(wg_buf *b, unsigned long cp)
{
  char u[4];

  if(cp==0 || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) {
    cp= 0xfffd;
  }
  if(cp<0x80) {
    u[0]= (char)cp;
    wg_buf_add(b, u, 1);
  } else if(cp<0x800) {
    u[0]= (char)(0xc0|(cp>>6));
    u[1]= (char)(0x80|(cp&0x3f));
    wg_buf_add(b, u, 2);
  } else if(cp<0x10000) {
    u[0]= (char)(0xe0|(cp>>12));
    u[1]= (char)(0x80|((cp>>6)&0x3f));
    u[2]= (char)(0x80|(cp&0x3f));
    wg_buf_add(b, u, 3);
  } else {
    u[0]= (char)(0xf0|(cp>>18));
    u[1]= (char)(0x80|((cp>>12)&0x3f));
    u[2]= (char)(0x80|((cp>>6)&0x3f));
    u[3]= (char)(0x80|(cp&0x3f));
    wg_buf_add(b, u, 4);
  }
}

size_t wg_extract_urls_html(const char *html, size_t len, const char *base_url, wg_url_cb cb, void *ctx)
{
  sink_t s= {cb, ctx, NULL, 0, 0, 0};
  char *source= NULL;
  const char *base= base_url, *p, *end= html+len, *frag, *tag, *next_bare;
  size_t hdr;
  int bare_known;

  /* CF_HTML header ("Version:0.9 ... SourceURL:...") comes before the first '<' */
  hdr= 0;
  while(hdr<len && html[hdr]!='<') {
    hdr++;
  }
  if((p= find_ci(html, hdr, "sourceurl:"))) {
    size_t rem, sl= 0;

    p+= 10;
    rem= hdr-(size_t)(p-html); /* the buffer needn't be NUL-terminated */
    while(sl<rem && p[sl]!='\r' && p[sl]!='\n') {
      sl++;
    }
    source= wg_strndup(p, sl);
    if(wg_url_is_supported(source)) {
      base= source;
    }
  }
  if((frag= find_ci(html, len, "<!--startfragment"))) {
    const char *close= find_ci(frag, (size_t)(end-frag), "-->");

    html= close? close+3: frag+17;
    if((tag= find_ci(html, (size_t)(end-html), "<!--endfragment"))) {
      end= tag;
    }
  }

  /* Scan for href=, src= and bare URLs, whichever comes first (same as 1.2.x) */
  p= html;
  next_bare= NULL;
  bare_known= 0;
  while(p<end) {
    size_t rest= (size_t)(end-p);
    const char *attr= NULL;
    long bare;
    size_t alen= 0, i;

    if(!bare_known || (next_bare && next_bare<p)) { /* (re)find the next bare URL at most once per step */
      long at= next_scheme(p, rest, 0);

      next_bare= (at>=0)? p+at: NULL;
      bare_known= 1;
    }
    bare= next_bare? (long)(next_bare-p): -1;

    for(i= 0; i<rest; i++) {
      if((bare>=0 && i>=(size_t)bare)) {
        break;
      }
      if((p[i]=='h' || p[i]=='H') && wg_prefix_ci(p+i, rest-i, "href")) {
        attr= p+i;
        alen= 4;
        break;
      }
      if((p[i]=='s' || p[i]=='S') && wg_prefix_ci(p+i, rest-i, "src")) {
        attr= p+i;
        alen= 3;
        break;
      }
    }
    if(attr) {
      const char *v= attr+alen;
      char quote= 0;
      const char *vend;
      char *raw, *url;

      while(v<end && (*v==' ' || *v=='\t' || *v=='\r' || *v=='\n')) {
        v++;
      }
      if(v>=end || *v!='=') {
        p= attr+alen;
        continue; /* just the word "href"/"src" in text */
      }
      v++;
      while(v<end && (*v==' ' || *v=='\t' || *v=='\r' || *v=='\n')) {
        v++;
      }
      if(v<end && (*v=='"' || *v=='\'')) {
        quote= *v++;
      }
      vend= v;
      while(vend<end && (quote? *vend!=quote: !(*vend==' ' || *vend=='\t' || *vend=='\r' ||
                                                  *vend=='\n' || *vend=='>'))) {
        vend++;
      }
      if((size_t)(vend-v)<=WG_URL_MAX) {
        raw= decode_entities(v, (size_t)(vend-v));
        if(!skip_link(raw)) {
          url= wg_url_resolve(base, raw);
          sink_add(&s, url);
        }
        wg_free(raw);
      }
      p= (vend<end)? vend+1: end;
    } else if(bare>=0) {
      size_t n= url_span(p+bare, rest-(size_t)bare);

      if(n>0 && n<=WG_URL_MAX) {
        char *raw= decode_entities(p+bare, n);

        sink_add(&s, raw);
      }
      p+= (size_t)bare+(n? n: 1);
    } else {
      break;
    }
  }
  wg_free(source);
  sink_done(&s);
  return s.count;
}
