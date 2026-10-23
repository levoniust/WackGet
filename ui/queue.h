/*
 * The download queue: the list of items and the once-a-second update that the original did in
 * on_timer_update() (list.c). No window code here; the views read the texts it prepares.
 * Copyright (C) 2002-2003 Millweed, later Mammoth Media; GPLv2.
 */
#ifndef WACKGET_UI_QUEUE_H
#define WACKGET_UI_QUEUE_H

#include <memory>
#include <string>
#include <vector>

#include "wackget/core.h"

enum { COL_NAME, COL_STATUS, COL_PROGRESS, COL_SPEED, COL_COUNT };
extern const char *const column_names[COL_COUNT];

struct Item {
  std::string url;
  wg_status status= WG_STAT_QUEUED;
  int flags= 0;
  wg_progress prog= {};
  std::string name;    /* local file name once the download knows it */
  std::string path;    /* full local path once known */
  std::string dir;     /* download folder when it was started (original TODO in on_deleteitem) */
  std::string message; /* last error in words */
  wg_dl *dl= nullptr;  /* running (or stopping) download */
  std::string text[COL_COUNT];
  double fraction= -1; /* 0..1 for the skinned progress bars, -1 while the size is unknown */
  bool selected= false;

  ~Item();
  bool is_partial() const { return status!=WG_STAT_DONE && (prog.start>0 || prog.cur>0); }
  std::string local_path(const std::string &download_dir) const;
};

/* The settings the update reads each tick (original update_info_t). */
struct QueuePrefs {
  std::string download_dir;
  int max_threads= 0;
  bool auto_resume= true;
  bool clear_completed= false;
  bool log= false;
  std::string log_path;
  std::string proxy, proxy_user, proxy_pass; /* empty when the proxy is off */
};

struct QueueTotals {
  int downloading= 0; /* going + paused, as the original counted them */
  int queued= 0;
  int64_t speed= 0;
  bool changed_rows= false; /* items were removed (cleared) during the update */
};

class Queue {
public:
  ~Queue();

  size_t size() const { return items_.size(); }
  Item &operator[](size_t i) { return *items_[i]; }
  const Item &operator[](size_t i) const { return *items_[i]; }

  /* Adds an item (already checked by wg_url_is_supported) to the end. */
  void add(const std::string &url, int status= WG_STAT_QUEUED, int flags= 0);
  /* Removes items; running downloads are stopped. When delete_partial, partial files are removed
     once their download has ended (retried later if the file is still busy, like TIMER_DELETEFILES). */
  void remove(const std::vector<size_t> &rows, bool delete_partial, const std::string &download_dir);
  void clear();
  /* Moves the rows by diff places (original move_selection); false if any would leave the list. */
  bool can_move(const std::vector<size_t> &rows, int diff) const;
  void move(std::vector<size_t> &rows, int diff);

  /* One tick of the original timer: check downloads, clear/log finished ones, start queued ones,
     refresh the texts. */
  QueueTotals update(const QueuePrefs &p);
  /* Stops everything and waits for the threads (exit). */
  void shutdown();

  std::vector<wg_queue_item> snapshot_for_save() const;

private:
  struct Leftover { wg_dl *dl; std::string path; int tries; };
  void start(Item &it, bool resume, const QueuePrefs &p);
  void reap_leftovers();

  std::vector<std::unique_ptr<Item>> items_;
  std::vector<Leftover> leftovers_;
};

/* Puts every supported URL found in plain text / HTML into out (deduplicated by the core). */
std::vector<std::string> urls_from_text(const std::string &text);
std::vector<std::string> urls_from_html(const std::string &html, const char *base_url);

#endif
