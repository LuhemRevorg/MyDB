#pragma once
#include <optional>
#include <string>
#include <vector>
#include "common/types.h"
#include "storage/buffer_pool.h"

class BPlusTree {
 public:
  // leaf_max_size / internal_max_size default to page capacity.
  // Pass smaller values in tests to trigger splits with few keys.
  BPlusTree(std::string name, BufferPoolManager* bpm,
            int leaf_max_size     = 0,
            int internal_max_size = 0);

  void Insert(int32_t key, RID rid);
  bool Search(int32_t key, RID* rid) const;
  void RangeScan(int32_t low, int32_t high, std::vector<RID>& results) const;

  bool      IsEmpty()      const { return root_page_id_ == INVALID_PAGE_ID; }
  page_id_t GetRootPageId() const { return root_page_id_; }

 private:
  struct SplitResult { int32_t key; page_id_t right_pid; };

  // Returns a SplitResult if the subtree rooted at pid split.
  std::optional<SplitResult> InsertInPage(page_id_t pid, int32_t key, RID rid);
  std::optional<SplitResult> InsertInLeaf(page_id_t pid, int32_t key, RID rid);
  std::optional<SplitResult> InsertInInternal(page_id_t pid, int32_t key, RID rid);

  page_id_t FindLeafPage(int32_t key) const;

  std::string        name_;
  BufferPoolManager* bpm_;
  page_id_t          root_page_id_{INVALID_PAGE_ID};
  int                leaf_max_size_;
  int                internal_max_size_;
};
