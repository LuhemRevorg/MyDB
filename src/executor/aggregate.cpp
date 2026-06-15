#include "executor/aggregate.h"
#include <algorithm>
#include <cstdint>

HashAggregate::HashAggregate(std::unique_ptr<Operator>  child,
                             std::vector<const Expr*>   group_by,
                             std::vector<AggExpr>       agg_exprs,
                             Schema                     output_schema)
    : child_{std::move(child)},
      group_by_{std::move(group_by)},
      agg_exprs_{std::move(agg_exprs)},
      output_schema_{std::move(output_schema)} {}

std::string HashAggregate::MakeKey(const Tuple& t) const {
  std::string key;
  const Schema& schema = child_->GetOutputSchema();
  for (const Expr* e : group_by_)
    key += EvalExpr(e, t, schema).ToString() + "|";
  return key;
}

void HashAggregate::Init() {
  groups_.clear();
  group_order_.clear();
  cursor_ = 0;

  child_->Init();
  Tuple t;
  const Schema& schema = child_->GetOutputSchema();
  const size_t n_agg   = agg_exprs_.size();

  while (child_->Next(&t)) {
    std::string key = MakeKey(t);

    if (!groups_.count(key)) {
      Group g;
      for (const Expr* e : group_by_)
        g.key_vals.push_back(EvalExpr(e, t, schema));
      g.counts.assign(n_agg, 0);
      g.sums.assign(n_agg, 0);
      g.mins.assign(n_agg, INT32_MAX);
      g.maxs.assign(n_agg, INT32_MIN);
      groups_[key] = std::move(g);
      group_order_.push_back(key);
    }

    Group& g = groups_[key];
    for (size_t i = 0; i < n_agg; ++i) {
      const AggExpr& ae = agg_exprs_[i];
      g.counts[i]++;
      if (ae.func != "COUNT") {
        int32_t v = EvalExpr(ae.arg, t, schema).AsInt();
        g.sums[i] += v;
        if (v < g.mins[i]) g.mins[i] = v;
        if (v > g.maxs[i]) g.maxs[i] = v;
      }
    }
  }
  child_->Close();
}

bool HashAggregate::Next(Tuple* out) {
  if (cursor_ >= group_order_.size()) return false;
  const Group& g = groups_[group_order_[cursor_++]];

  std::vector<Value> vals;
  for (const Value& v : g.key_vals) vals.push_back(v);
  for (size_t i = 0; i < agg_exprs_.size(); ++i) {
    const AggExpr& ae = agg_exprs_[i];
    if      (ae.func == "COUNT") vals.push_back(Value::MakeInt(static_cast<int32_t>(g.counts[i])));
    else if (ae.func == "SUM")   vals.push_back(Value::MakeInt(static_cast<int32_t>(g.sums[i])));
    else if (ae.func == "AVG")   vals.push_back(Value::MakeInt(static_cast<int32_t>(g.sums[i] / (g.counts[i] > 0 ? g.counts[i] : 1))));
    else if (ae.func == "MIN")   vals.push_back(Value::MakeInt(g.mins[i]));
    else if (ae.func == "MAX")   vals.push_back(Value::MakeInt(g.maxs[i]));
  }
  *out = Tuple{std::move(vals), output_schema_};
  return true;
}

void HashAggregate::Close() { groups_.clear(); group_order_.clear(); }
