#pragma once
#include <atomic>
#include <mutex>
#include <vector>
#include "executor/executor.h"
#include "executor/thread_pool.h"
#include "catalog/catalog.h"

// Parallel sequential scan using an atomic page counter.
//
// In Init(), N worker threads each atomically claim the next unscanned page
// via fetch_add on a shared counter. No locking needed between workers —
// each thread owns its page exclusively once claimed.
// Results are collected into per-thread local buffers then merged.
class ParallelSeqScan : public Operator {
 public:
  ParallelSeqScan(TableInfo* table_info, std::string alias, size_t n_threads);

  void Init()           override;
  bool Next(Tuple* out) override;
  void Close()          override;
  const Schema& GetOutputSchema() const override { return output_schema_; }

 private:
  TableInfo*         table_info_;
  Schema             output_schema_;
  size_t             n_threads_;
  std::vector<Tuple> buffer_;
  size_t             cursor_{0};
};
