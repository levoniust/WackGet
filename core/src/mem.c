#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wackget/core.h"

static void out_of_memory(void)
{
  fputs("WackGet: out of memory\n", stderr);
  abort();
}

void *wg_malloc(size_t n)
{
  void *p= malloc(n? n: 1);

  if(!p) {
    out_of_memory();
  }
  return p;
}

void *wg_realloc(void *p, size_t n)
{
  p= realloc(p, n? n: 1);
  if(!p) {
    out_of_memory();
  }
  return p;
}

void wg_free(void *p)
{
  free(p);
}

char *wg_strndup(const char *s, size_t n)
{
  char *d= wg_malloc(n+1);

  memcpy(d, s, n);
  d[n]= '\0';
  return d;
}

char *wg_strdup(const char *s)
{
  return wg_strndup(s, strlen(s));
}
