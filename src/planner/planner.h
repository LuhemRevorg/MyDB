#pragma once
#include <memory>
#include <string>
#include "executor/executor.h"
#include "executor/aggregate.h"
#include "catalog/catalog.h"
#include "parser/ast.h"

class Planner {
 public:
  explicit Planner(Catalog* catalog);

  std::unique_ptr<Operator> Plan(const SelectStmt& stmt);

  // Returns a human-readable plan tree for EXPLAIN.
  std::string Explain(const SelectStmt& stmt);

 private:
  // Determine whether an expression references only columns from one table.
  static bool HasAggregate(const std::vector<SelectItem>& list);
  static Schema BuildProjectionSchema(const std::vector<SelectItem>& list,
                                      const Schema& input);
  static Schema BuildAggSchema(const std::vector<const Expr*>& group_by,
                               const std::vector<AggExpr>& aggs,
                               const Schema& input);

  Catalog* catalog_;
};
