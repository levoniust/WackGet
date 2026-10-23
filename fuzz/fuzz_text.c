/* Fuzz target: URLs from pasted text, plus the URL helpers on arbitrary strings. */
#include <stdlib.h>
#include <string.h>

#include "wackget/core.h"

static void check(const char *url, void *ctx)
{
  (void)ctx;
  if(!wg_url_is_supported(url)) {
    abort();
  }
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
  char *copy= malloc(size? size: 1), *str= malloc(size+1), *r;

  memcpy(copy, data, size);
  wg_extract_urls_text(copy, size, check, NULL);
  memcpy(str, data, size);
  str[size]= '\0';
  wg_url_is_supported(str);
  if((r= wg_url_resolve("http://a/b/c/d;p?q", str))) {
    check(r, NULL);
    wg_free(r);
  }
  if(wg_url_is_supported(str) && (r= wg_url_resolve(str, "../x/./y?z"))) {
    check(r, NULL);
    wg_free(r);
  }
  if((r= wg_url_referer(str))) {
    check(r, NULL);
    wg_free(r);
  }
  free(copy);
  free(str);
  return 0;
}
