#pragma once
#include <unordered_map>
#include <vector>
#include "executor/executor.h"
#include "parser/ast.h"

struct AggExpr {
  std::string func;        // COUNT SUM AVG MIN MAX
  const Expr* arg{nullptr}; // nullptr when func=COUNT and is_star=true
  bool        is_star{false};
  std::string output_name;
};

class HashAggregate : public Operator {
 public:
  HashAggregate(std::unique_ptr<Operator>  child,
                std::vector<const Expr*>   group_by,
                std::vector<AggExpr>       agg_exprs,
                Schema                     output_schema);

  void Init()           override;
  bool Next(Tuple* out) override;
  void Close()          override;
  const Schema& GetOutputSchema() const override { return output_schema_; }

 private:
  struct Group {
    std::vector<Value> key_vals;   // group-by column values
    std::vector<int64_t> counts;
    std::vector<int64_t> sums;
    std::vector<int32_t> mins;
    std::vector<int32_t> maxs;
  };

  std::string MakeKey(const Tuple& t) const;

  std::unique_ptr<Operator>  child_;
  std::vector<const Expr*>   group_by_;
  std::vector<AggExpr>       agg_exprs_;
  Schema                     output_schema_;

  std::unordered_map<std::string, Group> groups_;
  std::vector<std::string>               group_order_;  // insertion order
  size_t                                 cursor_{0};
};
