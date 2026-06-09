#include "storage/buffer_pool.h"

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_manager)
    : pool_size_{pool_size},
      disk_manager_{disk_manager},
      frames_{std::make_unique<Page[]>(pool_size)},
      replacer_{std::make_unique<LRUReplacer>(pool_size)} {
  for (size_t i = 0; i < pool_size; ++i)
    free_list_.push_back(static_cast<frame_id_t>(i));
}

BufferPoolManager::~BufferPoolManager() {
  FlushAllPages();
}

frame_id_t BufferPoolManager::GetFreeFrame() {
  if (!free_list_.empty()) {
    frame_id_t fid = free_list_.front();
    free_list_.pop_front();
    return fid;
  }
  frame_id_t fid;
  if (replacer_->Evict(&fid)) {
    Page& page = frames_[fid];
    if (page.is_dirty_) {
      disk_manager_->WritePage(page.page_id_, page.data_);
      page.is_dirty_ = false;
    }
    page_table_.erase(page.page_id_);
    page.page_id_ = INVALID_PAGE_ID;
    return fid;
  }
  return -1;
}

Page* BufferPoolManager::NewPage(page_id_t* page_id) {
  std::scoped_lock lock(latch_);
  frame_id_t fid = GetFreeFrame();
  if (fid == -1) return nullptr;

  *page_id = disk_manager_->AllocatePage();

  Page& page = frames_[fid];
  page.ResetMemory();
  page.page_id_   = *page_id;
  page.pin_count_ = 1;
  page.is_dirty_  = false;

  page_table_[*page_id] = fid;
  replacer_->RecordAccess(fid);
  replacer_->SetEvictable(fid, false);

  return &page;
}

Page* BufferPoolManager::FetchPage(page_id_t page_id) {
  std::scoped_lock lock(latch_);

  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    frame_id_t fid = it->second;
    Page& page = frames_[fid];
    page.pin_count_++;
    replacer_->RecordAccess(fid);
    replacer_->SetEvictable(fid, false);
    return &page;
  }

  frame_id_t fid = GetFreeFrame();
  if (fid == -1) return nullptr;

  Page& page = frames_[fid];
  page.page_id_   = page_id;
  page.pin_count_ = 1;
  page.is_dirty_  = false;
  disk_manager_->ReadPage(page_id, page.data_);

  page_table_[page_id] = fid;
  replacer_->RecordAccess(fid);
  replacer_->SetEvictable(fid, false);

  return &page;
}

bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
  std::scoped_lock lock(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return false;

  Page& page = frames_[it->second];
  if (page.pin_count_ == 0) return false;

  page.is_dirty_ |= is_dirty;
  if (--page.pin_count_ == 0)
    replacer_->SetEvictable(it->second, true);

  return true;
}

bool BufferPoolManager::FlushPage(page_id_t page_id) {
  std::scoped_lock lock(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return false;

  Page& page = frames_[it->second];
  disk_manager_->WritePage(page_id, page.data_);
  page.is_dirty_ = false;
  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::scoped_lock lock(latch_);
  for (auto& [page_id, fid] : page_table_) {
    Page& page = frames_[fid];
    if (page.is_dirty_) {
      disk_manager_->WritePage(page_id, page.data_);
      page.is_dirty_ = false;
    }
  }
}

bool BufferPoolManager::DeletePage(page_id_t page_id) {
  std::scoped_lock lock(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) return true;

  frame_id_t fid = it->second;
  Page& page = frames_[fid];
  if (page.pin_count_ > 0) return false;

  page_table_.erase(it);
  replacer_->Remove(fid);
  free_list_.push_back(fid);
  page.ResetMemory();
  page.page_id_  = INVALID_PAGE_ID;
  page.is_dirty_ = false;
  return true;
}
