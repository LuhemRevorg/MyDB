#pragma once
#include <vector>
#include "executor/executor.h"
#include "parser/ast.h"

class Projection : public Operator {
 public:
  // exprs[i] is evaluated to produce output column i.
  // col_names[i] is the name of output column i.
  Projection(std::unique_ptr<Operator> child,
             std::vector<const Expr*>  exprs,
             std::vector<std::string>  col_names,
             Schema                    output_schema);

  void Init()           override;
  bool Next(Tuple* out) override;
  void Close()          override;
  const Schema& GetOutputSchema() const override { return output_schema_; }

 private:
  std::unique_ptr<Operator> child_;
  std::vector<const Expr*>  exprs_;
  std::vector<std::string>  col_names_;
  Schema                    output_schema_;
};
