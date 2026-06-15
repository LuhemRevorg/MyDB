#include "concurrency/lockfree_hashtable.h"

LockFreeHashTable::LockFreeHashTable(size_t bucket_count)
    : bucket_count_{bucket_count}, buckets_(bucket_count) {
  for (auto& b : buckets_) b.store(nullptr, std::memory_order_relaxed);
}

LockFreeHashTable::~LockFreeHashTable() { Clear(); }

void LockFreeHashTable::Insert(int32_t key, Tuple value) {
  auto* node = new Node{key, std::move(value)};
  size_t idx = Hash(key);

  // Prepend at bucket head with a CAS retry loop.
  // memory_order_release: ensures node data is visible to threads
  //   that subsequently load the pointer with acquire.
  Node* head = buckets_[idx].load(std::memory_order_relaxed);
  do {
    node->next.store(head, std::memory_order_relaxed);
  } while (!buckets_[idx].compare_exchange_weak(
      head, node,
      std::memory_order_release,
      std::memory_order_relaxed));
}

void LockFreeHashTable::Lookup(int32_t key, std::vector<const Tuple*>& out) const {
  size_t idx = Hash(key);
  // memory_order_acquire: pairs with the release in Insert, ensures we see
  // fully-constructed nodes.
  const Node* cur = buckets_[idx].load(std::memory_order_acquire);
  while (cur) {
    if (cur->key == key) out.push_back(&cur->value);
    cur = cur->next.load(std::memory_order_relaxed);
  }
}

void LockFreeHashTable::Clear() {
  for (auto& bucket : buckets_) {
    Node* cur = bucket.exchange(nullptr, std::memory_order_relaxed);
    while (cur) {
      Node* next = cur->next.load(std::memory_order_relaxed);
      delete cur;
      cur = next;
    }
  }
}
