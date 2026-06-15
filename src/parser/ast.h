#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "common/types.h"

// ── Expressions ───────────────────────────────────────────────────────────────

enum class ExprType {
  COLUMN_REF,     // [table.]col
  INT_LITERAL,
  STRING_LITERAL,
  BINARY_OP,      // left op right  (=, <, >, <=, >=, !=, AND, OR)
  UNARY_OP,       // op child       (NOT)
  STAR,           // bare * in SELECT *
  AGGREGATE,      // COUNT/SUM/AVG/MIN/MAX(expr or *)
};

struct Expr {
  ExprType type;

  // COLUMN_REF
  std::string table_alias;
  std::string col_name;

  // INT_LITERAL
  int32_t int_val{0};

  // STRING_LITERAL
  std::string str_val;

  // BINARY_OP / UNARY_OP
  std::string op;
  std::unique_ptr<Expr> left;
  std::unique_ptr<Expr> right;  // nullptr for unary

  // AGGREGATE
  std::string func_name;   // "COUNT", "SUM", etc.
  bool        is_star_arg{false};
};

// ── Statements ────────────────────────────────────────────────────────────────

struct ColumnDef {
  std::string name;
  TypeId      type_id{TypeId::INT};
  uint16_t    max_len{0};
};

struct CreateTableStmt {
  std::string            table_name;
  std::vector<ColumnDef> columns;
};

struct InsertStmt {
  std::string            table_name;
  std::vector<std::unique_ptr<Expr>> values;  // INT_LITERAL or STRING_LITERAL
};

struct SelectItem {
  std::unique_ptr<Expr> expr;
  std::string           alias;  // from AS clause, may be empty
};

struct JoinClause {
  std::string           table_name;
  std::string           alias;
  std::unique_ptr<Expr> condition;  // ON expr
};

struct OrderByItem {
  std::unique_ptr<Expr> expr;
  bool                  ascending{true};
};

struct SelectStmt {
  std::vector<SelectItem>  select_list;
  std::string              from_table;
  std::string              from_alias;
  std::vector<JoinClause>  joins;
  std::unique_ptr<Expr>    where_clause;
  std::vector<std::unique_ptr<Expr>> group_by;
  std::vector<OrderByItem> order_by;
};

struct ExplainStmt {
  // The inner statement is always a SelectStmt for now.
  std::unique_ptr<SelectStmt> select;
};

using Statement = std::variant<CreateTableStmt, InsertStmt, SelectStmt, ExplainStmt>;
