#include "executor/parallel_seq_scan.h"

static Schema MakeOutputSchema(const Schema& base, const std::string& alias) {
  if (alias.empty()) return base;
  std::vector<Column> cols;
  for (size_t i = 0; i < base.GetColumnCount(); ++i) {
    Column c = base.GetColumn(i);
    c.name = alias + "." + c.name;
    cols.push_back(c);
  }
  return Schema{cols};
}

ParallelSeqScan::ParallelSeqScan(TableInfo* table_info, std::string alias, size_t n_threads)
    : table_info_{table_info},
      output_schema_{MakeOutputSchema(table_info->schema, alias)},
      n_threads_{n_threads} {}

void ParallelSeqScan::Init() {
  buffer_.clear();
  cursor_ = 0;

  // Collect the page chain once (sequential, cheap — just pointer hops).
  std::vector<page_id_t> page_ids = table_info_->heap_file->GetPageIds();
  if (page_ids.empty()) return;

  // Shared atomic counter: each worker claims the next unscanned page index.
  // alignas(64) prevents false sharing between the counter and thread stacks.
  alignas(64) std::atomic<size_t> next_page_idx{0};

  // Per-thread local result buffers — no sharing during scan.
  const size_t n = std::min(n_threads_, page_ids.size());
  std::vector<std::vector<Tuple>> local(n);

  // Mutex only used to merge results after all threads finish.
  ThreadPool pool(n);
  for (size_t tid = 0; tid < n; ++tid) {
    pool.Submit([&, tid] {
      while (true) {
        // Atomically claim the next page — relaxed ordering is correct here:
        // each page index is independent; there is no data dependency between
        // the counter load and the page contents.
        size_t idx = next_page_idx.fetch_add(1, std::memory_order_relaxed);
        if (idx >= page_ids.size()) break;

        table_info_->heap_file->ScanPage(page_ids[idx], [&](const RID&, const Tuple& t) {
          local[tid].push_back(t);
        });
      }
    });
  }
  pool.WaitAll();

  // Merge per-thread buffers into the shared result buffer.
  for (auto& vec : local)
    for (auto& t : vec)
      buffer_.push_back(std::move(t));
}

bool ParallelSeqScan::Next(Tuple* out) {
  if (cursor_ >= buffer_.size()) return false;
  *out = buffer_[cursor_++];
  return true;
}

void ParallelSeqScan::Close() { buffer_.clear(); }
