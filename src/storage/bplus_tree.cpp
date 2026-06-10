#include "storage/bplus_tree.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>

// ── Page layout structs ───────────────────────────────────────────────────────
//
// Both structs are overlaid directly on Page::GetData() via reinterpret_cast.
// They must be standard-layout POD types. No constructors — use Init() instead.
// page_type is always the first field so we can read it without knowing the type.

static constexpr uint32_t NODE_INTERNAL = 0;
static constexpr uint32_t NODE_LEAF     = 1;

struct InternalEntry {
  int32_t   key;
  page_id_t child;
};

struct LeafEntry {
  int32_t key;
  RID     rid;  // 8 bytes: page_id_t (4) + uint32_t slot (4)
};

// Internal page:
//   data[0].key   = unused (dummy)
//   data[0].child = leftmost subtree
//   data[i].key   = i-th separator key  (i >= 1)
//   data[i].child = right child of separator i
//
// Invariant: subtree at data[i].child holds keys in
//   [data[i].key, data[i+1].key)
struct BPlusTreeInternalPage {
  static constexpr int HEADER_SIZE = 12;
  static constexpr int CAPACITY    = (PAGE_SIZE - HEADER_SIZE) / static_cast<int>(sizeof(InternalEntry));
  // 12 + 510 * 8 = 4092 <= 4096 ✓

  uint32_t  page_type;
  int32_t   size;           // number of entries (including dummy at [0])
  page_id_t parent_page_id;
  InternalEntry data[CAPACITY];

  void Init(page_id_t parent = INVALID_PAGE_ID) {
    page_type      = NODE_INTERNAL;
    size           = 1;  // starts with just the dummy entry
    parent_page_id = parent;
    data[0]        = {0, INVALID_PAGE_ID};
  }

  // Find the child page_id that should contain key.
  page_id_t Lookup(int32_t key) const {
    int lo = 1, hi = size - 1, result = 0;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      if (data[mid].key <= key) { result = mid; lo = mid + 1; }
      else                        hi = mid - 1;
    }
    return data[result].child;
  }

  // Insert (new_key, new_right_child) at the correct sorted position.
  void InsertEntry(int32_t new_key, page_id_t new_right) {
    int pos = size;
    for (int i = 1; i < size; ++i) {
      if (data[i].key > new_key) { pos = i; break; }
    }
    for (int i = size; i > pos; --i) data[i] = data[i - 1];
    data[pos] = {new_key, new_right};
    ++size;
  }
};
static_assert(sizeof(BPlusTreeInternalPage) <= PAGE_SIZE, "Internal page exceeds PAGE_SIZE");

struct BPlusTreeLeafPage {
  static constexpr int HEADER_SIZE = 16;
  static constexpr int CAPACITY    = (PAGE_SIZE - HEADER_SIZE) / static_cast<int>(sizeof(LeafEntry));
  // 16 + 340 * 12 = 4096 ✓

  uint32_t  page_type;
  int32_t   size;
  page_id_t parent_page_id;
  page_id_t next_page_id;
  LeafEntry data[CAPACITY];

  void Init(page_id_t parent = INVALID_PAGE_ID) {
    page_type      = NODE_LEAF;
    size           = 0;
    parent_page_id = parent;
    next_page_id   = INVALID_PAGE_ID;
  }

  // Insert (key, rid) maintaining sorted key order.
  void InsertEntry(int32_t key, RID rid) {
    int pos = size;
    for (int i = 0; i < size; ++i) {
      if (data[i].key > key) { pos = i; break; }
    }
    for (int i = size; i > pos; --i) data[i] = data[i - 1];
    data[pos] = {key, rid};
    ++size;
  }
};
static_assert(sizeof(BPlusTreeLeafPage) <= PAGE_SIZE, "Leaf page exceeds PAGE_SIZE");

// ── Cast helpers ──────────────────────────────────────────────────────────────

static BPlusTreeInternalPage* AsInternal(Page* p) {
  return reinterpret_cast<BPlusTreeInternalPage*>(p->GetData());
}
static BPlusTreeLeafPage* AsLeaf(Page* p) {
  return reinterpret_cast<BPlusTreeLeafPage*>(p->GetData());
}
static uint32_t PageType(Page* p) {
  return *reinterpret_cast<const uint32_t*>(p->GetData());
}

// ── BPlusTree ─────────────────────────────────────────────────────────────────

BPlusTree::BPlusTree(std::string name, BufferPoolManager* bpm,
                     int leaf_max_size, int internal_max_size)
    : name_{std::move(name)},
      bpm_{bpm},
      leaf_max_size_    {leaf_max_size     > 0 ? leaf_max_size     : BPlusTreeLeafPage::CAPACITY},
      internal_max_size_{internal_max_size > 0 ? internal_max_size : BPlusTreeInternalPage::CAPACITY} {
  assert(leaf_max_size_     >= 2);
  assert(internal_max_size_ >= 3);
}

// ── Insert ────────────────────────────────────────────────────────────────────

void BPlusTree::Insert(int32_t key, RID rid) {
  if (root_page_id_ == INVALID_PAGE_ID) {
    page_id_t pid;
    Page* page = bpm_->NewPage(&pid);
    if (!page) throw std::runtime_error("BPlusTree::Insert — buffer pool full");
    AsLeaf(page)->Init();
    AsLeaf(page)->InsertEntry(key, rid);
    bpm_->UnpinPage(pid, true);
    root_page_id_ = pid;
    return;
  }

  auto result = InsertInPage(root_page_id_, key, rid);
  if (!result) return;

  // Root split — create a new internal root.
  page_id_t new_root_pid;
  Page* new_root_page = bpm_->NewPage(&new_root_pid);
  if (!new_root_page) throw std::runtime_error("BPlusTree::Insert — buffer pool full on root split");

  BPlusTreeInternalPage* new_root = AsInternal(new_root_page);
  new_root->Init();
  new_root->data[0].child = root_page_id_;
  new_root->InsertEntry(result->key, result->right_pid);

  bpm_->UnpinPage(new_root_pid, true);
  root_page_id_ = new_root_pid;
}

std::optional<BPlusTree::SplitResult> BPlusTree::InsertInPage(page_id_t pid, int32_t key, RID rid) {
  Page* page = bpm_->FetchPage(pid);
  if (!page) throw std::runtime_error("BPlusTree — failed to fetch page");

  if (PageType(page) == NODE_LEAF) {
    bpm_->UnpinPage(pid, false);  // InsertInLeaf will re-fetch
    return InsertInLeaf(pid, key, rid);
  }
  bpm_->UnpinPage(pid, false);
  return InsertInInternal(pid, key, rid);
}

std::optional<BPlusTree::SplitResult> BPlusTree::InsertInLeaf(page_id_t pid, int32_t key, RID rid) {
  Page* page = bpm_->FetchPage(pid);
  BPlusTreeLeafPage* leaf = AsLeaf(page);

  // Overwrite if key already exists.
  for (int i = 0; i < leaf->size; ++i) {
    if (leaf->data[i].key == key) {
      leaf->data[i].rid = rid;
      bpm_->UnpinPage(pid, true);
      return std::nullopt;
    }
  }

  leaf->InsertEntry(key, rid);

  if (leaf->size < leaf_max_size_) {
    bpm_->UnpinPage(pid, true);
    return std::nullopt;
  }

  // ── Leaf split ──
  page_id_t new_pid;
  Page* new_page = bpm_->NewPage(&new_pid);
  if (!new_page) throw std::runtime_error("BPlusTree — buffer pool full during leaf split");
  BPlusTreeLeafPage* new_leaf = AsLeaf(new_page);
  new_leaf->Init();

  int mid = leaf->size / 2;
  new_leaf->size = leaf->size - mid;
  for (int i = 0; i < new_leaf->size; ++i)
    new_leaf->data[i] = leaf->data[mid + i];

  new_leaf->next_page_id = leaf->next_page_id;
  leaf->next_page_id     = new_pid;
  leaf->size             = mid;

  int32_t separator = new_leaf->data[0].key;

  bpm_->UnpinPage(new_pid, true);
  bpm_->UnpinPage(pid,     true);
  return SplitResult{separator, new_pid};
}

std::optional<BPlusTree::SplitResult> BPlusTree::InsertInInternal(page_id_t pid, int32_t key, RID rid) {
  Page* page = bpm_->FetchPage(pid);
  BPlusTreeInternalPage* node = AsInternal(page);

  page_id_t child_pid = node->Lookup(key);
  bpm_->UnpinPage(pid, false);  // release before recursing

  auto result = InsertInPage(child_pid, key, rid);
  if (!result) return std::nullopt;

  // Child split — insert separator into this node.
  page = bpm_->FetchPage(pid);
  node = AsInternal(page);
  node->InsertEntry(result->key, result->right_pid);

  if (node->size <= internal_max_size_) {
    bpm_->UnpinPage(pid, true);
    return std::nullopt;
  }

  // ── Internal split ──
  page_id_t new_pid;
  Page* new_page = bpm_->NewPage(&new_pid);
  if (!new_page) throw std::runtime_error("BPlusTree — buffer pool full during internal split");
  BPlusTreeInternalPage* new_node = AsInternal(new_page);
  new_node->Init();

  int mid = node->size / 2;
  int32_t push_up_key = node->data[mid].key;

  // New node's leftmost child = right child of the pushed-up key.
  new_node->data[0].child = node->data[mid].child;
  new_node->size = 1;
  for (int i = mid + 1; i < node->size; ++i)
    new_node->data[new_node->size++] = node->data[i];

  node->size = mid;  // old node loses everything from mid onwards

  bpm_->UnpinPage(new_pid, true);
  bpm_->UnpinPage(pid,     true);
  return SplitResult{push_up_key, new_pid};
}

// ── Search ────────────────────────────────────────────────────────────────────

bool BPlusTree::Search(int32_t key, RID* rid) const {
  if (root_page_id_ == INVALID_PAGE_ID) return false;

  page_id_t pid = root_page_id_;
  while (true) {
    Page* page = bpm_->FetchPage(pid);
    if (!page) return false;

    if (PageType(page) == NODE_LEAF) {
      BPlusTreeLeafPage* leaf = AsLeaf(page);
      int lo = 0, hi = leaf->size - 1;
      while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if      (leaf->data[mid].key == key) { *rid = leaf->data[mid].rid; bpm_->UnpinPage(pid, false); return true; }
        else if (leaf->data[mid].key  < key) lo = mid + 1;
        else                                 hi = mid - 1;
      }
      bpm_->UnpinPage(pid, false);
      return false;
    }

    BPlusTreeInternalPage* node = AsInternal(page);
    page_id_t next = node->Lookup(key);
    bpm_->UnpinPage(pid, false);
    pid = next;
  }
}

// ── Range scan ────────────────────────────────────────────────────────────────

void BPlusTree::RangeScan(int32_t low, int32_t high, std::vector<RID>& results) const {
  if (root_page_id_ == INVALID_PAGE_ID) return;

  // Traverse to the leaf that could contain 'low'.
  page_id_t pid = root_page_id_;
  while (true) {
    Page* page = bpm_->FetchPage(pid);
    if (PageType(page) == NODE_LEAF) { bpm_->UnpinPage(pid, false); break; }
    BPlusTreeInternalPage* node = AsInternal(page);
    page_id_t next = node->Lookup(low);
    bpm_->UnpinPage(pid, false);
    pid = next;
  }

  // Walk the leaf chain collecting keys in [low, high].
  while (pid != INVALID_PAGE_ID) {
    Page* page = bpm_->FetchPage(pid);
    BPlusTreeLeafPage* leaf = AsLeaf(page);
    page_id_t next = leaf->next_page_id;

    bool done = false;
    for (int i = 0; i < leaf->size; ++i) {
      if (leaf->data[i].key > high) { done = true; break; }
      if (leaf->data[i].key >= low)  results.push_back(leaf->data[i].rid);
    }

    bpm_->UnpinPage(pid, false);
    if (done) break;
    pid = next;
  }
}
