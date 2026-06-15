#pragma once
#include "executor/executor.h"

class Filter : public Operator {
 public:
  Filter(std::unique_ptr<Operator> child, const Expr* predicate);

  void Init()           override;
  bool Next(Tuple* out) override;
  void Close()          override;
  const Schema& GetOutputSchema() const override { return child_->GetOutputSchema(); }

 private:
  std::unique_ptr<Operator> child_;
  const Expr*               predicate_;
};
