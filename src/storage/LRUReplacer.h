#pragma once
  #include <list>
  #include <unordered_map>
  #include <unordered_set>
  #include "common/config.h"

  class LRUReplacer {
   public:
    explicit LRUReplacer(size_t num_frames);

    bool   Evict(frame_id_t* frame_id);
    void   RecordAccess(frame_id_t frame_id);
    void   SetEvictable(frame_id_t frame_id, bool evictable);
    void   Remove(frame_id_t frame_id);
    size_t Size() const;

   private:
    size_t num_frames_;
    std::list<frame_id_t> lru_list_;
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;
    std::unordered_set<frame_id_t> evictable_;
};