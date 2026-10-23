/* Fuzz target: links from pasted/dropped HTML (CF_HTML and plain text/html). The input buffer is not
   NUL-terminated on purpose. Every URL that comes out must pass wg_url_is_supported. */
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
  char *copy= malloc(size? size: 1); /* exact size: reading past the end is caught by ASan */

  memcpy(copy, data, size);
  wg_extract_urls_html(copy, size, NULL, check, NULL);
  wg_extract_urls_html(copy, size, "http://base.example/dir/page.html?q=1", check, NULL);
  free(copy);
  return 0;
}
