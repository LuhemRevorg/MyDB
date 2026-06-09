#include "storage/lru_replacer.h"

LRUReplacer::LRUReplacer(size_t num_frames) : num_frames_{num_frames} {}

bool LRUReplacer::Evict(frame_id_t* frame_id) {
  for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it) {
    if (evictable_.count(*it)) {
      *frame_id = *it;
      lru_map_.erase(*it);
      evictable_.erase(*it);
      lru_list_.erase(std::next(it).base());
      return true;
    }
  }
  return false;
}

void LRUReplacer::RecordAccess(frame_id_t frame_id) {
  auto it = lru_map_.find(frame_id);
  if (it != lru_map_.end()) {
    lru_list_.erase(it->second);
  }
  lru_list_.push_front(frame_id);
  lru_map_[frame_id] = lru_list_.begin();
}

void LRUReplacer::SetEvictable(frame_id_t frame_id, bool evictable) {
  if (evictable) {
    evictable_.insert(frame_id);
  } else {
    evictable_.erase(frame_id);
  }
}

void LRUReplacer::Remove(frame_id_t frame_id) {
  auto it = lru_map_.find(frame_id);
  if (it != lru_map_.end()) {
    lru_list_.erase(it->second);
    lru_map_.erase(it);
  }
  evictable_.erase(frame_id);
}

size_t LRUReplacer::Size() const {
  return evictable_.size();
}
