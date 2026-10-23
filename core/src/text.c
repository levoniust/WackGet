/* The Status, Progress and Speed column texts and the tray tooltip, worded exactly like 1.2.x
   (was list_util.c). */
#include <stdio.h>
#include <string.h>

#include "internal.h"

void wg_commafy(char *out, size_t outsz, int64_t val)
{
  char digits[32], tmp[48];
  size_t n, i, j= 0;
  int neg= val<0;
  uint64_t u= neg? (uint64_t)0-(uint64_t)val: (uint64_t)val;

  snprintf(digits, sizeof(digits), "%llu", (unsigned long long)u);
  n= strlen(digits);
  if(neg) {
    tmp[j++]= '-';
  }
  for(i= 0; i<n; i++) {
    if(i>0 && (n-i)%3==0) {
      tmp[j++]= ',';
    }
    tmp[j++]= digits[i];
  }
  tmp[j]= '\0';
  snprintf(out, outsz, "%s", tmp);
}

void wg_text_status(char *out, size_t outsz, wg_status st, const wg_progress *p)
{
  static const char *const text[WG_STAT_COUNT]= {"Queued", "Downloading", "Paused", "Complete", "Failed"};

  if((int)st<0 || st>=WG_STAT_COUNT) {
    snprintf(out, outsz, "?");
  } else if(st==WG_STAT_FAILED && p && p->error>0) {
    snprintf(out, outsz, "%s (%d)", text[st], p->error);
  } else {
    snprintf(out, outsz, "%s", text[st]);
  }
}

int wg_text_progress(char *out, size_t outsz, const wg_progress *p)
{
  char cur[32], total[32];

  if(p->total<1) {
    return -1;
  }
  wg_commafy(total, sizeof(total), p->total/1024);
  if(p->cur<p->total) {
    wg_commafy(cur, sizeof(cur), p->cur/1024);
    snprintf(out, outsz, "%sK/%sK; %.0f%%", cur, total, (double)p->cur/(double)p->total*100.0);
  } else {
    snprintf(out, outsz, "%sK; %.0f%%", total, (double)p->cur/(double)p->total*100.0);
  }
  return 0;
}

int64_t wg_speed(const wg_progress *p)
{
  if(p->elapsed<1000) { /* Don't try to calculate speed until at least a second has passed */
    return -1;
  }
  return (p->cur-p->start)/1024/(p->elapsed/1000);
}

int64_t wg_time_left(const wg_progress *p, int64_t speed)
{
  if(speed<1 || p->total<1) {
    return -1;
  }
  return (p->total-p->cur)/1024/speed;
}

void wg_text_speed(char *out, size_t outsz, wg_status st, int64_t speed, int64_t time_left)
{
  char sp[32];

  if(st!=WG_STAT_GOING || speed<0) {
    snprintf(out, outsz, "%s", ""); /* Blank if not downloading */
    return;
  }
  wg_commafy(sp, sizeof(sp), speed);
  if(time_left<0) {
    snprintf(out, outsz, "%sK/sec; ?", sp);
  } else {
    snprintf(out, outsz, "%sK/sec; %lld:%02lld", sp, (long long)(time_left/60), (long long)(time_left%60));
  }
}

void wg_text_tooltip(char *out, size_t outsz, int downloading, int queued, int64_t total_speed)
{
  char a[32], b[32], c[32];

  wg_commafy(a, sizeof(a), downloading);
  wg_commafy(b, sizeof(b), queued);
  wg_commafy(c, sizeof(c), total_speed);
  snprintf(out, outsz, "%s downloading; %s queued; %sK/sec total", a, b, c);
}
