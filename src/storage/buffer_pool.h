#pragma once
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "storage/disk_manager.h"
#include "storage/lru_replacer.h"
#include "storage/page.h"

class BufferPoolManager {
 public:
  BufferPoolManager(size_t pool_size, DiskManager* disk_manager);
  ~BufferPoolManager();

  Page* NewPage(page_id_t* page_id);
  Page* FetchPage(page_id_t page_id);
  bool  UnpinPage(page_id_t page_id, bool is_dirty);
  bool  FlushPage(page_id_t page_id);
  void  FlushAllPages();
  bool  DeletePage(page_id_t page_id);

  size_t GetPoolSize() const { return pool_size_; }

 private:
  frame_id_t GetFreeFrame();

  size_t                                    pool_size_;
  DiskManager*                              disk_manager_;
  std::unique_ptr<Page[]>                   frames_;
  std::unordered_map<page_id_t, frame_id_t> page_table_;
  std::list<frame_id_t>                     free_list_;
  std::unique_ptr<LRUReplacer>              replacer_;
  std::mutex                                latch_;
};
