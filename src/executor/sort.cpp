#include "executor/sort.h"
#include <algorithm>

Sort::Sort(std::unique_ptr<Operator> child, std::vector<const OrderByItem*> order_by)
    : child_{std::move(child)}, order_by_{std::move(order_by)} {}

void Sort::Init() {
  buffer_.clear();
  cursor_ = 0;

  child_->Init();
  Tuple t;
  while (child_->Next(&t)) buffer_.push_back(t);
  child_->Close();

  const Schema& schema = child_->GetOutputSchema();
  std::sort(buffer_.begin(), buffer_.end(), [&](const Tuple& a, const Tuple& b) {
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
  });
}

bool Sort::Next(Tuple* out) {
  if (cursor_ >= buffer_.size()) return false;
  *out = buffer_[cursor_++];
  return true;
}

void Sort::Close() { buffer_.clear(); }
