#include "planner/planner.h"
#include <sstream>
#include <stdexcept>
#include "executor/seq_scan.h"
#include "executor/filter.h"
#include "executor/projection.h"
#include "executor/hash_join.h"
#include "executor/sort.h"
#include "executor/aggregate.h"

Planner::Planner(Catalog* catalog) : catalog_{catalog} {}

bool Planner::HasAggregate(const std::vector<SelectItem>& list) {
  for (const auto& item : list)
    if (item.expr && item.expr->type == ExprType::AGGREGATE) return true;
  return false;
}

// Build output schema for a Projection from a select list of COLUMN_REFs.
Schema Planner::BuildProjectionSchema(const std::vector<SelectItem>& list,
                                      const Schema& input) {
  std::vector<Column> cols;
  for (const auto& item : list) {
    std::string name = item.alias;
    ColumnType  type = ColumnType::Int();
    if (item.expr->type == ExprType::COLUMN_REF) {
      std::string key = item.expr->table_alias.empty() ? item.expr->col_name
                      : item.expr->table_alias + "." + item.expr->col_name;
      size_t idx = input.GetColumnIdx(key);
      type = input.GetColumn(idx).type;
      if (name.empty()) name = item.expr->col_name;
    } else if (name.empty()) {
      name = "?";
    }
    cols.push_back({name, type});
  }
  return Schema{cols};
}

Schema Planner::BuildAggSchema(const std::vector<const Expr*>& group_by,
                               const std::vector<AggExpr>& aggs,
                               const Schema& input) {
  std::vector<Column> cols;
  for (const Expr* e : group_by) {
    std::string key = e->table_alias.empty() ? e->col_name
                    : e->table_alias + "." + e->col_name;
    size_t idx = input.GetColumnIdx(key);
    cols.push_back(input.GetColumn(idx));
    // Use unqualified name in output
    cols.back().name = e->col_name;
  }
  for (const AggExpr& ae : aggs)
    cols.push_back({ae.output_name, ColumnType::Int()});
  return Schema{cols};
}

std::unique_ptr<Operator> Planner::Plan(const SelectStmt& stmt) {
  // ── 1. Base scan (+ optional joins) ──────────────────────────────────────

  TableInfo* from_info = catalog_->GetTable(stmt.from_table);
  std::string from_alias = stmt.from_alias.empty() ? stmt.from_table : stmt.from_alias;
  std::unique_ptr<Operator> plan = std::make_unique<SeqScan>(from_info, from_alias);

  for (const auto& join : stmt.joins) {
    TableInfo* join_info = catalog_->GetTable(join.table_name);
    std::string join_alias = join.alias.empty() ? join.table_name : join.alias;
    auto right = std::make_unique<SeqScan>(join_info, join_alias);

    // Extract left_key / right_key from equality join condition.
    // Supports: a = b where one side refs left table and other refs right.
    const Expr* cond = join.condition.get();
    if (!cond || cond->type != ExprType::BINARY_OP || cond->op != "=")
      throw std::runtime_error("Only equality join conditions are supported");

    plan = std::make_unique<HashJoin>(
        std::move(plan), std::move(right),
        cond->left.get(), cond->right.get());
  }

  // ── 2. Filter ─────────────────────────────────────────────────────────────

  if (stmt.where_clause)
    plan = std::make_unique<Filter>(std::move(plan), stmt.where_clause.get());

  // ── 3. Aggregate or Projection ───────────────────────────────────────────

  const Schema& current_schema = plan->GetOutputSchema();
  bool is_star = (stmt.select_list.size() == 1 &&
                  stmt.select_list[0].expr->type == ExprType::STAR);

  if (!is_star && (HasAggregate(stmt.select_list) || !stmt.group_by.empty())) {
    // Build group-by expression list
    std::vector<const Expr*> group_exprs;
    for (const auto& e : stmt.group_by) group_exprs.push_back(e.get());

    // Build aggregate expression list from select items
    std::vector<AggExpr> agg_exprs;
    for (const auto& item : stmt.select_list) {
      if (item.expr->type == ExprType::AGGREGATE) {
        AggExpr ae;
        ae.func        = item.expr->func_name;
        ae.is_star     = item.expr->is_star_arg;
        ae.arg         = item.expr->is_star_arg ? nullptr : item.expr->left.get();
        ae.output_name = item.alias.empty() ? item.expr->func_name : item.alias;
        agg_exprs.push_back(ae);
      }
    }
    Schema agg_schema = BuildAggSchema(group_exprs, agg_exprs, current_schema);
    plan = std::make_unique<HashAggregate>(
        std::move(plan), std::move(group_exprs), std::move(agg_exprs), std::move(agg_schema));

  } else if (!is_star) {
    // Sort before Projection so ORDER BY can reference non-projected columns.
    if (!stmt.order_by.empty()) {
      std::vector<const OrderByItem*> items;
      for (const auto& item : stmt.order_by) items.push_back(&item);
      plan = std::make_unique<Sort>(std::move(plan), std::move(items));
    }

    std::vector<const Expr*> exprs;
    std::vector<std::string> names;
    for (const auto& item : stmt.select_list) {
      exprs.push_back(item.expr.get());
      names.push_back(item.alias.empty() && item.expr->type == ExprType::COLUMN_REF
                      ? item.expr->col_name : item.alias);
    }
    Schema proj_schema = BuildProjectionSchema(stmt.select_list, plan->GetOutputSchema());
    plan = std::make_unique<Projection>(
        std::move(plan), std::move(exprs), std::move(names), std::move(proj_schema));
    return plan;  // Sort already applied above
  }

  // ── 4. Sort (for SELECT * and aggregate queries) ──────────────────────────

  if (!stmt.order_by.empty()) {
    std::vector<const OrderByItem*> items;
    for (const auto& item : stmt.order_by) items.push_back(&item);
    plan = std::make_unique<Sort>(std::move(plan), std::move(items));
  }

  return plan;
}

std::string Planner::Explain(const SelectStmt& stmt) {
  std::ostringstream oss;
  bool has_agg  = HasAggregate(stmt.select_list) || !stmt.group_by.empty();
  bool is_star  = (stmt.select_list.size() == 1 && stmt.select_list[0].expr->type == ExprType::STAR);
  int depth = 0;
  auto indent = [&]() { return std::string(depth * 2, ' '); };

  if (!stmt.order_by.empty()) { oss << indent() << "Sort\n"; ++depth; }
  if (has_agg)                { oss << indent() << "HashAggregate\n"; ++depth; }
  else if (!is_star)          { oss << indent() << "Projection\n"; ++depth; }
  if (stmt.where_clause)      { oss << indent() << "Filter\n"; ++depth; }
  if (!stmt.joins.empty()) {
    oss << indent() << "HashJoin\n"; ++depth;
    oss << indent() << "SeqScan [" << stmt.from_table << "]\n";
    for (const auto& j : stmt.joins)
      oss << indent() << "SeqScan [" << j.table_name << "]\n";
  } else {
    oss << indent() << "SeqScan [" << stmt.from_table << "]\n";
  }
  return oss.str();
}
