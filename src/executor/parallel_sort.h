#pragma once
#include <functional>
#include <vector>
#include "executor/executor.h"
#include "parser/ast.h"

// Parallel sort: each thread sorts a partition using std::sort (introsort),
// then a k-way merge via a min-heap combines the sorted runs into a single
// output stream.
class ParallelSort : public Operator {
 public:
  ParallelSort(std::unique_ptr<Operator>       child,
               std::vector<const OrderByItem*> order_by,
               size_t                          n_threads);

  void Init()           override;
  bool Next(Tuple* out) override;
  void Close()          override;
  const Schema& GetOutputSchema() const override { return child_->GetOutputSchema(); }

 private:
  using Comparator = std::function<bool(const Tuple&, const Tuple&)>;
  Comparator MakeComparator(const Schema& schema) const;

  std::unique_ptr<Operator>       child_;
  std::vector<const OrderByItem*> order_by_;
  size_t                          n_threads_;

  // Sorted partitions from the parallel sort phase.
  std::vector<std::vector<Tuple>> partitions_;

  // k-way merge state: one cursor per partition.
  struct HeapEntry {
    const Tuple* tuple;
    size_t       partition;
    size_t       index;
    bool operator>(const HeapEntry& o) const;  // for min-heap comparison
  };
  std::vector<HeapEntry> heap_;
  Comparator             cmp_;
  bool                   heap_ready_{false};
};
