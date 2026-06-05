#pragma once
#include <cstring>
#include <shared_mutex>
#include "common/config.h"

// One 4 KB buffer frame. Lives in the buffer pool's pre-allocated array.
// The data_ array is the raw page bytes — all higher layers read/write through it.
// pin_count_ > 0 means the page is in use and must not be evicted.
// is_dirty_  == true means data_ differs from disk and must be flushed before eviction.
class Page {
 public:
  char*       GetData()       { return data_; }
  const char* GetData() const { return data_; }
  page_id_t   GetPageId()     const { return page_id_; }
  int         GetPinCount()   const { return pin_count_; }
  bool        IsDirty()       const { return is_dirty_; }

  // Page-level latch for concurrent access (used from Week 4 onwards)
  void RLatch()   { latch_.lock_shared(); }
  void RUnlatch() { latch_.unlock_shared(); }
  void WLatch()   { latch_.lock(); }
  void WUnlatch() { latch_.unlock(); }

 private:
  friend class BufferPoolManager;

  void ResetMemory() { memset(data_, 0, PAGE_SIZE); }

  char              data_[PAGE_SIZE]{};
  page_id_t         page_id_{INVALID_PAGE_ID};
  int               pin_count_{0};
  bool              is_dirty_{false};
  std::shared_mutex latch_;
};
