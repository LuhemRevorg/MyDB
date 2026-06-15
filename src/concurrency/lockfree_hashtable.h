#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include "common/types.h"

// Lock-free hash table for the parallel hash join build phase.
//
// Inserts are concurrent (CAS at bucket head). Lookups are read-only and
// naturally thread-safe once all inserts are complete.
//
// Each bucket is a singly-linked list. Insert prepends at the head using
// a compare-exchange loop — the classic lock-free linked-list insert.
// No ABA risk here because nodes are never deleted during the build phase.
class LockFreeHashTable {
 public:
  explicit LockFreeHashTable(size_t bucket_count);
  ~LockFreeHashTable();

  // Thread-safe: multiple threads may call Insert concurrently.
  void Insert(int32_t key, Tuple value);

  // Safe after all Inserts are done (read-only traversal).
  void Lookup(int32_t key, std::vector<const Tuple*>& out) const;

  void Clear();

 private:
  struct Node {
    int32_t            key;
    Tuple              value;
    std::atomic<Node*> next{nullptr};
    Node(int32_t k, Tuple v) : key{k}, value{std::move(v)} {}
  };

  size_t Hash(int32_t key) const {
    // FNV-1a inspired mix for int keys
    auto u = static_cast<uint32_t>(key);
    u ^= u >> 16;
    u *= 0x45d9f3b;
    u ^= u >> 16;
    return static_cast<size_t>(u) % bucket_count_;
  }

  size_t                          bucket_count_;
  std::vector<std::atomic<Node*>> buckets_;
};
