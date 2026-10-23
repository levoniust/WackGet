/* Helpers shared by the core's own files (not part of the public API). */
#ifndef WG_INTERNAL_H
#define WG_INTERNAL_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "wackget/core.h"

/* Growing string buffer; always NUL-terminated. */
typedef struct {
  char *p;
  size_t len, cap;
} wg_buf;

static inline void wg_buf_add(wg_buf *b, const char *s, size_t n)
{
  if(n>((size_t)-1)/4-b->len) {
    abort(); /* can't happen with real inputs; keeps the size arithmetic provably in range */
  }
  if(b->len+n+1>b->cap) {
    b->cap= (b->len+n+1)*2;
    b->p= wg_realloc(b->p, b->cap);
  }
  memcpy(b->p+b->len, s, n);
  b->len+= n;
  b->p[b->len]= '\0';
}

static inline void wg_buf_addc(wg_buf *b, char c)
{
  wg_buf_add(b, &c, 1);
}

static inline void wg_buf_adds(wg_buf *b, const char *s)
{
  wg_buf_add(b, s, strlen(s));
}

static inline char wg_lower(char c)
{
  return (c>='A' && c<='Z')? (char)(c-'A'+'a'): c;
}

/* Case-insensitive ASCII prefix test. */
static inline int wg_prefix_ci(const char *s, size_t len, const char *pfx)
{
  size_t i;

  for(i= 0; pfx[i]; i++) {
    if(i>=len || wg_lower(s[i])!=pfx[i]) {
      return 0;
    }
  }
  return 1;
}

/* Appends the UTF-8 encoding of a code point (invalid ones become U+FFFD). */
void wg_buf_add_utf8(wg_buf *b, unsigned long cp);

#ifdef _WIN32
#include <wchar.h>
/* UTF-8 <-> UTF-16 for the Windows file APIs. Results are wg_malloc'ed. */
wchar_t *wg_utf8_to_wide(const char *s);
char *wg_wide_to_utf8(const wchar_t *w);
#endif

#endif
