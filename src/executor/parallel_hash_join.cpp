#include "executor/parallel_hash_join.h"
#include <cstring>
#include <mutex>
#include "executor/thread_pool.h"

static Schema ConcatSchemas(const Schema& l, const Schema& r) {
  std::vector<Column> cols;
  for (size_t i = 0; i < l.GetColumnCount(); ++i) cols.push_back(l.GetColumn(i));
  for (size_t i = 0; i < r.GetColumnCount(); ++i) cols.push_back(r.GetColumn(i));
  return Schema{cols};
}

ParallelHashJoin::ParallelHashJoin(std::unique_ptr<Operator> left,
                                   std::unique_ptr<Operator> right,
                                   const Expr* left_key,
                                   const Expr* right_key,
                                   size_t n_threads,
                                   size_t bucket_count)
    : left_{std::move(left)},
      right_{std::move(right)},
      left_key_{left_key},
      right_key_{right_key},
      n_threads_{n_threads},
      output_schema_{ConcatSchemas(left_->GetOutputSchema(), right_->GetOutputSchema())},
      hash_table_{bucket_count} {}

Tuple ParallelHashJoin::Concat(const Tuple& l, const Tuple& r,
                               const Schema& ls, const Schema& rs) {
  uint32_t total = ls.GetTupleSize() + rs.GetTupleSize();
  std::vector<char> buf(total);
  memcpy(buf.data(),                    l.GetData(), ls.GetTupleSize());
  memcpy(buf.data() + ls.GetTupleSize(), r.GetData(), rs.GetTupleSize());
  return Tuple::FromBytes(buf.data(), total);
}

void ParallelHashJoin::Init() {
  hash_table_.Clear();
  output_buffer_.clear();
  cursor_ = 0;

  // ── Build phase ──────────────────────────────────────────────────────────
  // Load all left tuples first (sequential), then insert in parallel.
  left_->Init();
  std::vector<Tuple> left_tuples;
  {
    Tuple t;
    while (left_->Next(&t)) left_tuples.push_back(t);
  }
  left_->Close();

  const Schema& left_schema = left_->GetOutputSchema();

  // Partition left tuples across threads; each thread inserts its slice.
  alignas(64) std::atomic<size_t> left_idx{0};
  {
    ThreadPool build_pool(n_threads_);
    for (size_t i = 0; i < n_threads_; ++i) {
      build_pool.Submit([&] {
        while (true) {
          size_t idx = left_idx.fetch_add(1, std::memory_order_relaxed);
          if (idx >= left_tuples.size()) break;
          const Tuple& t = left_tuples[idx];
          int32_t key = EvalExpr(left_key_, t, left_schema).AsInt();
          hash_table_.Insert(key, t);  // CAS-based, no mutex
        }
      });
    }
    build_pool.WaitAll();
  }

  // ── Probe phase ───────────────────────────────────────────────────────────
  // Load all right tuples, partition across threads, each probes independently.
  right_->Init();
  std::vector<Tuple> right_tuples;
  {
    Tuple t;
    while (right_->Next(&t)) right_tuples.push_back(t);
  }
  right_->Close();

  const Schema& right_schema  = right_->GetOutputSchema();
  const Schema& left_out      = left_->GetOutputSchema();
  const Schema& right_out     = right_->GetOutputSchema();

  std::vector<std::vector<Tuple>> local(n_threads_);
  alignas(64) std::atomic<size_t> right_idx{0};
  {
    ThreadPool probe_pool(n_threads_);
    for (size_t tid = 0; tid < n_threads_; ++tid) {
      probe_pool.Submit([&, tid] {
        while (true) {
          size_t idx = right_idx.fetch_add(1, std::memory_order_relaxed);
          if (idx >= right_tuples.size()) break;
          const Tuple& rt = right_tuples[idx];
          int32_t key = EvalExpr(right_key_, rt, right_schema).AsInt();
          std::vector<const Tuple*> matches;
          hash_table_.Lookup(key, matches);
          for (const Tuple* lt : matches)
            local[tid].push_back(Concat(*lt, rt, left_out, right_out));
        }
      });
    }
    probe_pool.WaitAll();
  }

  for (auto& vec : local)
    for (auto& t : vec)
      output_buffer_.push_back(std::move(t));
}

bool ParallelHashJoin::Next(Tuple* out) {
  if (cursor_ >= output_buffer_.size()) return false;
  *out = output_buffer_[cursor_++];
  return true;
}

void ParallelHashJoin::Close() {
  hash_table_.Clear();
  output_buffer_.clear();
}
