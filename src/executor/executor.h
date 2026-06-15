#pragma once
#include <memory>
#include <stdexcept>
#include "common/types.h"
#include "parser/ast.h"

// Volcano iterator interface — every physical operator implements these three methods.
class Operator {
 public:
  virtual ~Operator() = default;
  virtual void Init()              = 0;
  virtual bool Next(Tuple* out)    = 0;  // false = exhausted
  virtual void Close()             = 0;
  virtual const Schema& GetOutputSchema() const = 0;

  // ── Expression evaluation (shared by all operators) ──────────────────────

  static Value EvalExpr(const Expr* e, const Tuple& t, const Schema& schema) {
    switch (e->type) {
      case ExprType::INT_LITERAL:    return Value::MakeInt(e->int_val);
      case ExprType::STRING_LITERAL: return Value::MakeVarchar(e->str_val, 255);
      case ExprType::COLUMN_REF: {
        std::string key = e->table_alias.empty() ? e->col_name
                                                 : e->table_alias + "." + e->col_name;
        return t.GetValue(schema, schema.GetColumnIdx(key));
      }
      default:
        throw std::runtime_error("EvalExpr: unsupported expression type");
    }
  }

  static bool EvalPred(const Expr* e, const Tuple& t, const Schema& schema) {
    if (e->type == ExprType::UNARY_OP && e->op == "NOT")
      return !EvalPred(e->left.get(), t, schema);

    if (e->type == ExprType::BINARY_OP) {
      if (e->op == "AND") return EvalPred(e->left.get(), t, schema) && EvalPred(e->right.get(), t, schema);
      if (e->op == "OR")  return EvalPred(e->left.get(), t, schema) || EvalPred(e->right.get(), t, schema);

      Value lv = EvalExpr(e->left.get(),  t, schema);
      Value rv = EvalExpr(e->right.get(), t, schema);

      if (lv.GetType() == TypeId::INT && rv.GetType() == TypeId::INT) {
        int32_t l = lv.AsInt(), r = rv.AsInt();
        if (e->op == "=")  return l == r;
        if (e->op == "<")  return l <  r;
        if (e->op == ">")  return l >  r;
        if (e->op == "<=") return l <= r;
        if (e->op == ">=") return l >= r;
        if (e->op == "!=") return l != r;
      } else {
        const std::string& l = lv.AsVarchar(), &r = rv.AsVarchar();
        if (e->op == "=")  return l == r;
        if (e->op == "!=") return l != r;
        if (e->op == "<")  return l <  r;
        if (e->op == ">")  return l >  r;
        if (e->op == "<=") return l <= r;
        if (e->op == ">=") return l >= r;
      }
    }
    throw std::runtime_error("EvalPred: unsupported predicate");
  }
};
