/*
 * The download queue. update() follows the original on_timer_update(), check_item_status() and
 * spawn_download_if_possible() (list.c, list_util.c) with libcurl downloads in place of wget.exe.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#include "queue.h"

#include <algorithm>

const char *const column_names[COL_COUNT]= {"Name", "Status", "Progress", "Speed"};

Item::~Item()
{
  if(dl) { /* Queue hands running downloads to its leftovers first; this is only a safety net */
    wg_dl_stop(dl);
    wg_dl_free(dl);
  }
}

std::string Item::local_path(const std::string &download_dir) const
{
  if(!path.empty()) {
    return path;
  }
  char *n= wg_fs_name_from_url(url.c_str());
  char *p= wg_fs_join(dir.empty()? download_dir.c_str(): dir.c_str(), n);
  std::string s(p);

  wg_free(n);
  wg_free(p);
  return s;
}

Queue::~Queue()
{
  shutdown();
}

void Queue::add(const std::string &url, int status, int flags)
{
  auto it= std::make_unique<Item>();

  it->url= url;
  it->status= (status>=0 && status<WG_STAT_COUNT)? (wg_status)status: WG_STAT_QUEUED;
  it->flags= flags&WG_FLAG_MASK;
  it->text[COL_NAME]= url;
  items_.push_back(std::move(it));
}

void Queue::remove(const std::vector<size_t> &rows, bool delete_partial, const std::string &download_dir)
{
  std::vector<size_t> r(rows);

  std::sort(r.begin(), r.end());
  r.erase(std::unique(r.begin(), r.end()), r.end());
  for(auto i= r.rbegin(); i!=r.rend(); ++i) { /* in reverse, like on_edit_clear() */
    if(*i>=items_.size()) {
      continue;
    }
    Item &it= *items_[*i];
    Leftover lo{it.dl, std::string(), 0};

    if(delete_partial && it.is_partial()) {
      it.flags|= WG_FLAG_DELETE;
    }
    if(it.flags&WG_FLAG_DELETE) {
      lo.path= it.local_path(download_dir);
    }
    if(it.dl) {
      wg_dl_stop(it.dl);
      it.dl= nullptr;
    }
    if(lo.dl || !lo.path.empty()) {
      leftovers_.push_back(lo);
    }
    items_.erase(items_.begin()+(std::ptrdiff_t)*i);
  }
  reap_leftovers();
}

void Queue::clear()
{
  std::vector<size_t> all(items_.size());

  for(size_t i= 0; i<all.size(); i++) {
    all[i]= i;
  }
  remove(all, false, std::string());
}

bool Queue::can_move(const std::vector<size_t> &rows, int diff) const
{
  if(rows.empty()) {
    return false;
  }
  for(size_t r : rows) {
    long long n= (long long)r+diff;

    if(n<0 || n>=(long long)items_.size()) {
      return false;
    }
  }
  return true;
}

void Queue::move(std::vector<size_t> &rows, int diff)
{
  if(diff==0 || !can_move(rows, diff)) {
    return;
  }
  std::sort(rows.begin(), rows.end());
  /* One step at a time keeps the relative order of a scattered selection, like move_selection(). */
  for(int step= 0; step<std::abs(diff); step++) {
    if(diff>0) {
      for(auto i= rows.rbegin(); i!=rows.rend(); ++i) {
        std::swap(items_[*i], items_[*i+1]);
        (*i)++;
      }
    } else {
      for(auto &r : rows) {
        std::swap(items_[r], items_[r-1]);
        r--;
      }
    }
  }
}

void Queue::start(Item &it, bool resume, const QueuePrefs &p)
{
  wg_dl_opts o= {};

  o.url= it.url.c_str();
  o.dir= p.download_dir.c_str();
  o.resume= resume;
  if(!p.proxy.empty()) {
    o.proxy= p.proxy.c_str();
    o.proxy_user= p.proxy_user.empty()? nullptr: p.proxy_user.c_str();
    o.proxy_pass= p.proxy_pass.empty()? nullptr: p.proxy_pass.c_str();
  }
  o.retry_wait_ms= 1000; /* --waitretry=1 */
  o.max_tries= 0;        /* --tries=0 */
  wg_fs_mkdirs(o.dir);
  it.dir= p.download_dir;
  it.message.clear();
  it.dl= wg_dl_start(&o);
  it.status= WG_STAT_GOING;
}

void Queue::reap_leftovers()
{
  for(size_t i= 0; i<leftovers_.size();) {
    Leftover &lo= leftovers_[i];

    if(lo.dl) {
      wg_dl_info in;

      wg_dl_snapshot(lo.dl, &in);
      if(in.state==WG_DL_RUNNING) {
        i++;
        continue;
      }
      wg_dl_free(lo.dl);
      lo.dl= nullptr;
    }
    /* Files can stay locked for a moment (virus scanners on Windows): try again next tick, for a
       minute at most. */
    if(lo.path.empty() || wg_fs_remove(lo.path.c_str())==0 || ++lo.tries>60) {
      leftovers_.erase(leftovers_.begin()+(std::ptrdiff_t)i);
    } else {
      i++;
    }
  }
}

QueueTotals Queue::update(const QueuePrefs &p)
{
  QueueTotals t;

  reap_leftovers();

  /* Check up on the downloads (check_item_status) and count them */
  for(size_t i= 0; i<items_.size();) {
    Item &it= *items_[i];
    bool clear= false;

    if(it.dl) {
      wg_dl_info in;

      wg_dl_snapshot(it.dl, &in);
      it.prog= in.prog;
      if(in.name[0]) {
        it.name= in.name;
        it.path= in.path;
      }
      if(in.state==WG_DL_RUNNING) {
        if(it.status==WG_STAT_PAUSED) {
          wg_dl_stop(it.dl); /* Pause: the partial file stays for resuming */
        }
      } else {
        wg_dl_free(it.dl);
        it.dl= nullptr;
        it.message= in.message;
        if(in.state==WG_DL_DONE) {
          it.status= WG_STAT_DONE;
          if(p.log && !p.log_path.empty()) {
            wg_log_completed(p.log_path.c_str(), it.url.c_str());
          }
          clear= p.clear_completed;
        } else if(in.state==WG_DL_FAILED) {
          it.status= WG_STAT_FAILED;
        }
        /* WG_DL_STOPPED: paused, or unpaused before it had stopped (restarted below) */
      }
    }
    if(clear) {
      items_.erase(items_.begin()+(std::ptrdiff_t)i);
      t.changed_rows= true;
      continue;
    }
    switch(it.status) {
    case WG_STAT_GOING:
    case WG_STAT_PAUSED:
      t.downloading++;
      break;
    case WG_STAT_QUEUED:
      t.queued++;
      break;
    default:
      break;
    }
    i++;
  }

  /* Start what can be started (spawn_download_if_possible) and refresh the texts */
  for(auto &ip : items_) {
    Item &it= *ip;
    char buf[128];
    int64_t speed;

    if(!it.dl) {
      if(it.status==WG_STAT_QUEUED && (p.max_threads==0 || p.max_threads-t.downloading>0)) {
        start(it, p.auto_resume, p);
        t.downloading++;
        t.queued--;
      } else if(it.status==WG_STAT_GOING) {
        start(it, true, p); /* unpaused or forced: always continue */
      }
    }

    it.text[COL_NAME]= it.name.empty()? it.url: it.name;
    wg_text_status(buf, sizeof(buf), it.status, &it.prog);
    it.text[COL_STATUS]= buf;
    if(wg_text_progress(buf, sizeof(buf), &it.prog)==0) {
      it.text[COL_PROGRESS]= buf;
      it.fraction= (double)it.prog.cur/(double)it.prog.total;
    } else if(it.prog.cur>0) { /* size unknown (chunked): show what has arrived */
      wg_commafy(buf, sizeof(buf)-2, it.prog.cur/1024);
      it.text[COL_PROGRESS]= std::string(buf)+"K";
      it.fraction= -1;
    }
    if(it.status!=WG_STAT_GOING) {
      it.text[COL_SPEED].clear();
    } else if((speed= wg_speed(&it.prog))!=-1) {
      wg_text_speed(buf, sizeof(buf), it.status, speed, wg_time_left(&it.prog, speed));
      it.text[COL_SPEED]= buf;
      t.speed+= speed;
    }
  }
  return t;
}

void Queue::shutdown()
{
  for(auto &ip : items_) {
    if(ip->dl) {
      wg_dl_stop(ip->dl);
    }
  }
  for(auto &lo : leftovers_) {
    if(lo.dl) {
      wg_dl_stop(lo.dl);
    }
  }
  for(auto &ip : items_) {
    if(ip->dl) {
      wg_dl_free(ip->dl);
      ip->dl= nullptr;
    }
  }
  for(auto &lo : leftovers_) {
    if(lo.dl) {
      wg_dl_free(lo.dl);
    }
    if(!lo.path.empty()) {
      wg_fs_remove(lo.path.c_str()); /* one more try before exiting */
    }
  }
  leftovers_.clear();
}

std::vector<wg_queue_item> Queue::snapshot_for_save() const
{
  std::vector<wg_queue_item> v;

  for(auto &ip : items_) {
    wg_queue_item q;

    q.url= const_cast<char *>(ip->url.c_str());
    q.status= ip->status;
    q.flags= ip->flags;
    v.push_back(q);
  }
  return v;
}

static void push_url(const char *url, void *ctx)
{
  static_cast<std::vector<std::string> *>(ctx)->push_back(url);
}

std::vector<std::string> urls_from_text(const std::string &text)
{
  std::vector<std::string> v;

  wg_extract_urls_text(text.data(), text.size(), push_url, &v);
  return v;
}

std::vector<std::string> urls_from_html(const std::string &html, const char *base_url)
{
  std::vector<std::string> v;

  wg_extract_urls_html(html.data(), html.size(), base_url, push_url, &v);
  return v;
}
