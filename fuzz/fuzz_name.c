/* Fuzz target: local file names from URLs. Whatever the URL, the name must stay inside the download
   folder on every OS: no separators, no control characters, no "." / "..", no reserved Windows device
   name, valid UTF-8, at most 240 bytes. */
#include <stdlib.h>
#include <string.h>

#include "wackget/core.h"

static int valid_utf8(const unsigned char *s)
{
  while(*s) {
    int n= (*s<0x80)? 1: ((*s&0xe0)==0xc0)? 2: ((*s&0xf0)==0xe0)? 3: ((*s&0xf8)==0xf0)? 4: 0;
    int i;

    if(n==0) {
      return 0;
    }
    for(i= 1; i<n; i++) {
      if((s[i]&0xc0)!=0x80) {
        return 0;
      }
    }
    s+= n;
  }
  return 1;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
  char *url= malloc(size+1), *name;
  size_t i, n;

  memcpy(url, data, size);
  url[size]= '\0';
  name= wg_fs_name_from_url(url);
  n= strlen(name);
  if(n==0 || n>240 || strcmp(name, ".")==0 || strcmp(name, "..")==0 || !valid_utf8((unsigned char *)name) ||
     name[n-1]=='.' || name[n-1]==' ' || name[0]==' ') {
    abort();
  }
  for(i= 0; i<n; i++) {
    unsigned char c= (unsigned char)name[i];

    if(c<0x20 || c==0x7f || strchr("/\\:*?\"<>|", c)) {
      abort();
    }
  }
  wg_free(name);
  free(url);
  return 0;
}
