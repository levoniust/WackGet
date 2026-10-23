/* .wgq queue files (was queue_save.c). Format, unchanged since 1.2.x, little-endian:
 *   int32 version (12)
 *   repeated: int32 url_len, url bytes (no NUL), int32 status, int32 flags
 * Queue files come from anywhere (e-mail, downloads), so every field is checked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

static int32_t get_i32(const unsigned char *p)
{
  return (int32_t)((uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24));
}

static void put_i32(unsigned char *p, int32_t v)
{
  uint32_t u= (uint32_t)v;

  p[0]= (unsigned char)u;
  p[1]= (unsigned char)(u>>8);
  p[2]= (unsigned char)(u>>16);
  p[3]= (unsigned char)(u>>24);
}

int wg_queue_parse(const unsigned char *data, size_t len, wg_queue_cb cb, void *ctx, wg_queue_report *rep)
{
  wg_queue_report r= {0, 0, 0};
  size_t pos= 4;
  int32_t version;

  if(rep) {
    *rep= r;
  }
  if(len<4) {
    return WG_ERR_FORMAT;
  }
  version= get_i32(data);
  if(version<1 || version>WG_QUEUE_VERSION) {
    return WG_ERR_VERSION;
  }
  while(pos<len) {
    int32_t ulen, status, flags;
    wg_queue_item item;
    char *url;

    if(len-pos<4) {
      r.truncated= 1;
      break;
    }
    ulen= get_i32(data+pos);
    pos+= 4;
    if(ulen<1 || ulen>WG_URL_MAX || (size_t)ulen>len-pos || len-pos-(size_t)ulen<8) {
      r.truncated= 1; /* bad length or not enough bytes for url + status + flags */
      break;
    }
    url= wg_strndup((const char *)data+pos, (size_t)ulen);
    pos+= (size_t)ulen;
    status= get_i32(data+pos);
    flags= get_i32(data+pos+4);
    pos+= 8;

    if(strlen(url)!=(size_t)ulen || !wg_url_is_supported(url)) { /* embedded NUL or bad URL */
      r.skipped++;
      wg_free(url);
      continue;
    }
    if(status<0 || status>=WG_STAT_COUNT || status==WG_STAT_GOING) {
      status= WG_STAT_QUEUED; /* resumes through the queue, under MaxThreads */
    }
    item.url= url;
    item.status= status;
    item.flags= flags&WG_FLAG_MASK;
    if(cb) {
      cb(&item, ctx);
    }
    r.loaded++;
    wg_free(url);
  }
  if(rep) {
    *rep= r;
  }
  return WG_OK;
}

int wg_queue_load(const char *path, wg_queue_cb cb, void *ctx, wg_queue_report *rep)
{
  FILE *f;
  unsigned char *data;
  long size;
  int rv;

  if(rep) {
    memset(rep, 0, sizeof(*rep));
  }
  if(!(f= wg_fs_fopen(path, "rb"))) {
    return WG_ERR_IO;
  }
  if(fseek(f, 0, SEEK_END)!=0 || (size= ftell(f))<0 || fseek(f, 0, SEEK_SET)!=0) {
    fclose(f);
    return WG_ERR_IO;
  }
  if(size>WG_QUEUE_MAX_FILE) {
    fclose(f);
    return WG_ERR_TOOBIG;
  }
  data= wg_malloc((size_t)size);
  if(size>0 && fread(data, 1, (size_t)size, f)!=(size_t)size) {
    fclose(f);
    wg_free(data);
    return WG_ERR_IO;
  }
  fclose(f);
  rv= wg_queue_parse(data, (size_t)size, cb, ctx, rep);
  wg_free(data);
  return rv;
}

/* Writes to "<path>.tmp" and renames it over path, so a crash never leaves half a queue. */
int wg_replace_file(const char *tmp, const char *path);

int wg_queue_save(const char *path, const wg_queue_item *items, size_t n)
{
  char *tmp;
  FILE *f;
  unsigned char hdr[4];
  size_t i;
  int ok= 1;

  tmp= wg_malloc(strlen(path)+5);
  sprintf(tmp, "%s.tmp", path);
  if(!(f= wg_fs_fopen(tmp, "wb"))) {
    wg_free(tmp);
    return WG_ERR_IO;
  }
  put_i32(hdr, WG_QUEUE_VERSION);
  ok&= fwrite(hdr, 1, 4, f)==4;
  for(i= 0; i<n && ok; i++) {
    size_t ulen= strlen(items[i].url);
    unsigned char rec[8];

    if(ulen==0 || ulen>WG_URL_MAX) {
      continue;
    }
    put_i32(hdr, (int32_t)ulen);
    put_i32(rec, items[i].status);
    put_i32(rec+4, items[i].flags&WG_FLAG_MASK);
    ok&= fwrite(hdr, 1, 4, f)==4;
    ok&= fwrite(items[i].url, 1, ulen, f)==ulen;
    ok&= fwrite(rec, 1, 8, f)==8;
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
