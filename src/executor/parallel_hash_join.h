#pragma once
#include <vector>
#include "executor/executor.h"
#include "concurrency/lockfree_hashtable.h"

// Parallel hash join using a lock-free hash table for the build phase.
//
// Build: N threads concurrently insert left-child tuples into the
//   lock-free hash table (CAS at bucket head, no mutex).
// Probe: N threads concurrently probe the hash table (read-only, naturally safe).
//   Results are concatenated per thread then merged into a shared output buffer.
class ParallelHashJoin : public Operator {
 public:
  ParallelHashJoin(std::unique_ptr<Operator> left,
                   std::unique_ptr<Operator> right,
                   const Expr* left_key,
                   const Expr* right_key,
                   size_t n_threads,
                   size_t bucket_count = 65536);

  void Init()           override;
  bool Next(Tuple* out) override;
  void Close()          override;
  const Schema& GetOutputSchema() const override { return output_schema_; }

 private:
  static Tuple Concat(const Tuple& l, const Tuple& r,
                      const Schema& ls, const Schema& rs);

  std::unique_ptr<Operator> left_;
  std::unique_ptr<Operator> right_;
  const Expr*               left_key_;
  const Expr*               right_key_;
  size_t                    n_threads_;
  Schema                    output_schema_;
  LockFreeHashTable         hash_table_;
  std::vector<Tuple>        output_buffer_;
  size_t                    cursor_{0};
};
