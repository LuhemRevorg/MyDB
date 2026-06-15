#include "executor/parallel_sort.h"
#include <algorithm>
#include <queue>
#include "executor/thread_pool.h"

ParallelSort::ParallelSort(std::unique_ptr<Operator>       child,
                           std::vector<const OrderByItem*> order_by,
                           size_t                          n_threads)
    : child_{std::move(child)},
      order_by_{std::move(order_by)},
      n_threads_{n_threads} {}

ParallelSort::Comparator ParallelSort::MakeComparator(const Schema& schema) const {
  return [this, &schema](const Tuple& a, const Tuple& b) {
    for (const auto* item : order_by_) {
      Value va = EvalExpr(item->expr.get(), a, schema);
      Value vb = EvalExpr(item->expr.get(), b, schema);
      if (va.GetType() == TypeId::INT) {
        if (va.AsInt() != vb.AsInt())
          return item->ascending ? va.AsInt() < vb.AsInt() : va.AsInt() > vb.AsInt();
      } else {
        if (va.AsVarchar() != vb.AsVarchar())
          return item->ascending ? va.AsVarchar() < vb.AsVarchar() : va.AsVarchar() > vb.AsVarchar();
      }
    }
    return false;
  };
}

void ParallelSort::Init() {
  partitions_.clear();
  heap_.clear();
  heap_ready_ = false;

  // Load all input tuples.
  child_->Init();
  std::vector<Tuple> all;
  Tuple t;
  while (child_->Next(&t)) all.push_back(std::move(t));
  child_->Close();

  if (all.empty()) return;

  const Schema& schema = child_->GetOutputSchema();
  cmp_ = MakeComparator(schema);

  // Divide into n partitions, sort each in parallel.
  size_t n = std::min(n_threads_, all.size());
  partitions_.resize(n);
  size_t chunk = (all.size() + n - 1) / n;

  {
    ThreadPool pool(n);
    for (size_t i = 0; i < n; ++i) {
      pool.Submit([&, i] {
        size_t start = i * chunk;
        size_t end   = std::min(start + chunk, all.size());
        partitions_[i].assign(all.begin() + start, all.begin() + end);
        std::sort(partitions_[i].begin(), partitions_[i].end(), cmp_);
      });
    }
    pool.WaitAll();
  }

  // Build initial min-heap from the first element of each partition.
  // HeapEntry::operator> drives a min-heap (std::priority_queue is max by default,
  // so we invert the comparison to make it a min-heap).
  for (size_t i = 0; i < partitions_.size(); ++i) {
    if (!partitions_[i].empty())
      heap_.push_back({&partitions_[i][0], i, 0});
  }

  auto heap_cmp = [this](const HeapEntry& a, const HeapEntry& b) {
    return cmp_(*b.tuple, *a.tuple);  // inverted for min-heap
  };
  std::make_heap(heap_.begin(), heap_.end(), heap_cmp);
  heap_ready_ = true;
}

bool ParallelSort::Next(Tuple* out) {
  if (!heap_ready_ || heap_.empty()) return false;

  auto heap_cmp = [this](const HeapEntry& a, const HeapEntry& b) {
    return cmp_(*b.tuple, *a.tuple);
  };

  std::pop_heap(heap_.begin(), heap_.end(), heap_cmp);
  HeapEntry top = heap_.back();
  heap_.pop_back();

  *out = *top.tuple;

  // Advance that partition and push the next element into the heap.
  size_t next_idx = top.index + 1;
  if (next_idx < partitions_[top.partition].size()) {
    heap_.push_back({&partitions_[top.partition][next_idx], top.partition, next_idx});
    std::push_heap(heap_.begin(), heap_.end(), heap_cmp);
  }

  return true;
}

void ParallelSort::Close() {
  partitions_.clear();
  heap_.clear();
}
