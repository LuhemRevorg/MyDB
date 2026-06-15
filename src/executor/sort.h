#pragma once
#include <vector>
#include "executor/executor.h"
#include "parser/ast.h"

class Sort : public Operator {
 public:
  Sort(std::unique_ptr<Operator> child, std::vector<const OrderByItem*> order_by);

  void Init()           override;
  bool Next(Tuple* out) override;
  void Close()          override;
  const Schema& GetOutputSchema() const override { return child_->GetOutputSchema(); }

 private:
  std::unique_ptr<Operator>        child_;
  std::vector<const OrderByItem*>  order_by_;
  std::vector<Tuple>               buffer_;
  size_t                           cursor_{0};
};
