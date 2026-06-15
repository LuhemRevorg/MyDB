#include "executor/filter.h"

Filter::Filter(std::unique_ptr<Operator> child, const Expr* predicate)
    : child_{std::move(child)}, predicate_{predicate} {}

void Filter::Init()  { child_->Init(); }
void Filter::Close() { child_->Close(); }

bool Filter::Next(Tuple* out) {
  while (child_->Next(out)) {
    if (EvalPred(predicate_, *out, child_->GetOutputSchema()))
      return true;
  }
  return false;
}
