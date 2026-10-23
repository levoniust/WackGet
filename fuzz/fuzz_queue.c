/* Fuzz target: .wgq parsing. Every item that comes out must be safe to use. */
#include <stdlib.h>
#include <string.h>

#include "wackget/core.h"

static void check(const wg_queue_item *it, void *ctx)
{
  (void)ctx;
  if(!wg_url_is_supported(it->url) || it->status<0 || it->status>=WG_STAT_COUNT ||
     it->status==WG_STAT_GOING || (it->flags&~WG_FLAG_MASK)) {
    abort();
  }
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
  wg_queue_report rep;

  wg_queue_parse(data, size, check, NULL, &rep);
  return 0;
}
