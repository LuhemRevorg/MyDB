#include "executor/projection.h"
#include <cstring>

Projection::Projection(std::unique_ptr<Operator> child,
                       std::vector<const Expr*>  exprs,
                       std::vector<std::string>  col_names,
                       Schema                    output_schema)
    : child_{std::move(child)},
      exprs_{std::move(exprs)},
      col_names_{std::move(col_names)},
      output_schema_{std::move(output_schema)} {}

void Projection::Init()  { child_->Init(); }
void Projection::Close() { child_->Close(); }

bool Projection::Next(Tuple* out) {
  Tuple child_tuple;
  if (!child_->Next(&child_tuple)) return false;

  const Schema& in  = child_->GetOutputSchema();
  std::vector<Value> vals;
  vals.reserve(exprs_.size());
  for (const Expr* e : exprs_)
    vals.push_back(EvalExpr(e, child_tuple, in));

  *out = Tuple{std::move(vals), output_schema_};
  return true;
}
