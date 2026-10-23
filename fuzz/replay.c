/* Runs a fuzz target without libFuzzer (gcc, MSVC): every file in the corpus folder, then mutated
   copies of them and random inputs. Used by `ctest`; real fuzzing runs in CI with clang. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

typedef struct {
  unsigned char *data;
  size_t size;
} input_t;

static input_t seeds[256];
static int nseeds;

static void load(const char *path)
{
  FILE *f= fopen(path, "rb");
  long n;

  if(!f || nseeds>=256) {
    if(f) {
      fclose(f);
    }
    return;
  }
  fseek(f, 0, SEEK_END);
  n= ftell(f);
  fseek(f, 0, SEEK_SET);
  seeds[nseeds].data= malloc(n>0? (size_t)n: 1);
  seeds[nseeds].size= fread(seeds[nseeds].data, 1, n>0? (size_t)n: 0, f);
  fclose(f);
  nseeds++;
}

static void load_dir(const char *dir)
{
  char path[1024];
#ifdef _WIN32
  WIN32_FIND_DATAA fd;
  HANDLE h;

  snprintf(path, sizeof(path), "%s\\*", dir);
  if((h= FindFirstFileA(path, &fd))==INVALID_HANDLE_VALUE) {
    return;
  }
  do {
    if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
      snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
      load(path);
    }
  } while(FindNextFileA(h, &fd));
  FindClose(h);
#else
  DIR *d= opendir(dir);
  struct dirent *e;

  if(!d) {
    return;
  }
  while((e= readdir(d))) {
    if(e->d_name[0]!='.') {
      snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
      load(path);
    }
  }
  closedir(d);
#endif
}

static unsigned long long rng= 0x2545F4914F6CDD1DULL;

static unsigned rnd(void)
{
  rng^= rng<<13;
  rng^= rng>>7;
  rng^= rng<<17;
  return (unsigned)(rng>>11);
}

int main(int argc, char **argv)
{
  int i, j, runs= 0;
  unsigned char buf[8192];

  for(i= 1; i<argc; i++) {
    load_dir(argv[i]);
  }
  for(i= 0; i<nseeds; i++, runs++) {
    LLVMFuzzerTestOneInput(seeds[i].data, seeds[i].size);
  }
  for(i= 0; i<20000; i++, runs++) {
    size_t n;

    if(nseeds>0 && (i&1)) { /* mutate a seed: flip, insert or delete bytes */
      input_t *s= &seeds[rnd()%(unsigned)nseeds];

      n= s->size<sizeof(buf)? s->size: sizeof(buf);
      memcpy(buf, s->data, n);
      for(j= 0; j<1+(int)(rnd()%8); j++) {
        unsigned op= rnd()%3;

        if(n==0 || op==1) {
          if(n<sizeof(buf)) {
            size_t at= n? rnd()%n: 0;

            memmove(buf+at+1, buf+at, n-at);
            buf[at]= (unsigned char)rnd();
            n++;
          }
        } else if(op==0) {
          buf[rnd()%n]^= (unsigned char)(1u<<(rnd()%8));
        } else {
          size_t at= rnd()%n;

          memmove(buf+at, buf+at+1, n-at-1);
          n--;
        }
      }
      if(n>=4 && (rnd()%4)==0) { /* make a length field interesting */
        static const unsigned char vals[][4]= {{0xff, 0xff, 0xff, 0xff}, {0, 0, 0, 0x80}, {0xff, 0xff, 0xff, 0x7f}, {0, 0, 0, 0}};

        memcpy(buf+(rnd()%(n-3)), vals[rnd()%4], 4);
      }
    } else {
      n= rnd()%512;
      for(j= 0; j<(int)n; j++) {
        buf[j]= (rnd()%4)? (unsigned char)"htp:/sf.<>=\"'& #%a0\r\n"[rnd()%22]: (unsigned char)rnd();
      }
    }
    LLVMFuzzerTestOneInput(buf, n);
  }
  printf("%d inputs (%d seeds)\n", runs, nseeds);
  for(i= 0; i<nseeds; i++) {
    free(seeds[i].data);
  }
  return 0;
}
