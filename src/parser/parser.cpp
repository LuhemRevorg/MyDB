#include "parser/parser.h"
#include <stdexcept>
#include <unordered_map>

Parser::Parser(std::vector<Token> tokens) : tokens_{std::move(tokens)} {}

// ── Token helpers ─────────────────────────────────────────────────────────────

Token& Parser::Current()           { return tokens_[pos_]; }
Token& Parser::Peek(int offset)    { size_t i = pos_ + offset; return i < tokens_.size() ? tokens_[i] : tokens_.back(); }
Token  Parser::Consume()           { return tokens_[pos_++]; }
bool   Parser::Check(TokenType t) const { return tokens_[pos_].type == t; }

bool Parser::Match(TokenType t) {
  if (Check(t)) { ++pos_; return true; }
  return false;
}

Token Parser::Expect(TokenType type, const std::string& msg) {
  if (!Check(type))
    throw std::runtime_error("Parse error at '" + Current().value + "': " + msg);
  return Consume();
}

// ── Top-level ─────────────────────────────────────────────────────────────────

Statement Parser::Parse() {
  Statement stmt;
  if      (Match(TokenType::CREATE))  stmt = ParseCreateTable();
  else if (Match(TokenType::INSERT))  stmt = ParseInsert();
  else if (Match(TokenType::SELECT))  stmt = ParseSelect();
  else if (Match(TokenType::EXPLAIN)) stmt = ParseExplain();
  else throw std::runtime_error("Expected CREATE, INSERT, SELECT, or EXPLAIN");
  Match(TokenType::SEMICOLON);
  return stmt;
}

// ── CREATE TABLE ──────────────────────────────────────────────────────────────

CreateTableStmt Parser::ParseCreateTable() {
  Expect(TokenType::TABLE, "Expected TABLE");
  CreateTableStmt stmt;
  stmt.table_name = Expect(TokenType::IDENTIFIER, "Expected table name").value;
  Expect(TokenType::LPAREN, "Expected '('");

  do {
    ColumnDef col;
    col.name = Expect(TokenType::IDENTIFIER, "Expected column name").value;
    if (Match(TokenType::INT_KW)) {
      col.type_id = TypeId::INT;
    } else if (Match(TokenType::VARCHAR_KW)) {
      Expect(TokenType::LPAREN, "Expected '(' after VARCHAR");
      col.max_len = static_cast<uint16_t>(
          std::stoi(Expect(TokenType::INT_LITERAL, "Expected length").value));
      Expect(TokenType::RPAREN, "Expected ')'");
      col.type_id = TypeId::VARCHAR;
    } else {
      throw std::runtime_error("Expected type (INT or VARCHAR)");
    }
    stmt.columns.push_back(col);
  } while (Match(TokenType::COMMA));

  Expect(TokenType::RPAREN, "Expected ')'");
  return stmt;
}

// ── INSERT ────────────────────────────────────────────────────────────────────

InsertStmt Parser::ParseInsert() {
  Expect(TokenType::INTO, "Expected INTO");
  InsertStmt stmt;
  stmt.table_name = Expect(TokenType::IDENTIFIER, "Expected table name").value;
  Expect(TokenType::VALUES, "Expected VALUES");
  Expect(TokenType::LPAREN, "Expected '('");

  do {
    stmt.values.push_back(ParsePrimary());
  } while (Match(TokenType::COMMA));

  Expect(TokenType::RPAREN, "Expected ')'");
  return stmt;
}

// ── SELECT ────────────────────────────────────────────────────────────────────

SelectStmt Parser::ParseSelect() {
  SelectStmt stmt;
  stmt.select_list = ParseSelectList();

  Expect(TokenType::FROM, "Expected FROM");
  stmt.from_table = Expect(TokenType::IDENTIFIER, "Expected table name").value;
  if (Match(TokenType::AS))
    stmt.from_alias = Expect(TokenType::IDENTIFIER, "Expected alias").value;
  else if (Check(TokenType::IDENTIFIER) && !Check(TokenType::WHERE) &&
           !Check(TokenType::JOIN)      && !Check(TokenType::GROUP) &&
           !Check(TokenType::ORDER)     && !Check(TokenType::SEMICOLON) &&
           !Check(TokenType::EOF_TOKEN))
    stmt.from_alias = Consume().value;  // implicit alias without AS

  stmt.joins       = ParseJoins();
  stmt.where_clause= ParseWhere();
  stmt.group_by    = ParseGroupBy();
  stmt.order_by    = ParseOrderBy();
  return stmt;
}

std::vector<SelectItem> Parser::ParseSelectList() {
  std::vector<SelectItem> list;
  do { list.push_back(ParseSelectItem()); } while (Match(TokenType::COMMA));
  return list;
}

SelectItem Parser::ParseSelectItem() {
  SelectItem item;
  item.expr = ParseExpr();
  if (Match(TokenType::AS))
    item.alias = Expect(TokenType::IDENTIFIER, "Expected alias").value;
  return item;
}

std::vector<JoinClause> Parser::ParseJoins() {
  std::vector<JoinClause> joins;
  while (Match(TokenType::JOIN)) {
    JoinClause j;
    j.table_name = Expect(TokenType::IDENTIFIER, "Expected table name").value;
    if (Match(TokenType::AS))
      j.alias = Expect(TokenType::IDENTIFIER, "Expected alias").value;
    else if (Check(TokenType::IDENTIFIER))
      j.alias = Consume().value;  // implicit alias without AS
    Expect(TokenType::ON, "Expected ON");
    j.condition = ParseExpr();
    joins.push_back(std::move(j));
  }
  return joins;
}

std::unique_ptr<Expr> Parser::ParseWhere() {
  if (!Match(TokenType::WHERE)) return nullptr;
  return ParseExpr();
}

std::vector<std::unique_ptr<Expr>> Parser::ParseGroupBy() {
  std::vector<std::unique_ptr<Expr>> cols;
  if (!Match(TokenType::GROUP)) return cols;
  Expect(TokenType::BY, "Expected BY after GROUP");
  do { cols.push_back(ParsePrimary()); } while (Match(TokenType::COMMA));
  return cols;
}

std::vector<OrderByItem> Parser::ParseOrderBy() {
  std::vector<OrderByItem> items;
  if (!Match(TokenType::ORDER)) return items;
  Expect(TokenType::BY, "Expected BY after ORDER");
  do {
    OrderByItem item;
    item.expr      = ParsePrimary();
    item.ascending = !Match(TokenType::DESC);
    if (!item.ascending) {}  // DESC consumed
    else Match(TokenType::ASC);  // optional ASC keyword
    items.push_back(std::move(item));
  } while (Match(TokenType::COMMA));
  return items;
}

// ── EXPLAIN ───────────────────────────────────────────────────────────────────

ExplainStmt Parser::ParseExplain() {
  Expect(TokenType::SELECT, "EXPLAIN only supports SELECT");
  ExplainStmt stmt;
  stmt.select = std::make_unique<SelectStmt>(ParseSelect());
  return stmt;
}

// ── Expressions ───────────────────────────────────────────────────────────────
// Precedence (low → high): OR → AND → NOT → comparison → primary

std::unique_ptr<Expr> Parser::ParseExpr()       { return ParseOr(); }

std::unique_ptr<Expr> Parser::ParseOr() {
  auto left = ParseAnd();
  while (Match(TokenType::OR)) {
    auto right = ParseAnd();
    auto node  = std::make_unique<Expr>();
    node->type  = ExprType::BINARY_OP;
    node->op    = "OR";
    node->left  = std::move(left);
    node->right = std::move(right);
    left = std::move(node);
  }
  return left;
}

std::unique_ptr<Expr> Parser::ParseAnd() {
  auto left = ParseNot();
  while (Match(TokenType::AND)) {
    auto right = ParseNot();
    auto node  = std::make_unique<Expr>();
    node->type  = ExprType::BINARY_OP;
    node->op    = "AND";
    node->left  = std::move(left);
    node->right = std::move(right);
    left = std::move(node);
  }
  return left;
}

std::unique_ptr<Expr> Parser::ParseNot() {
  if (Match(TokenType::NOT)) {
    auto node  = std::make_unique<Expr>();
    node->type  = ExprType::UNARY_OP;
    node->op    = "NOT";
    node->left  = ParseComparison();
    return node;
  }
  return ParseComparison();
}

std::unique_ptr<Expr> Parser::ParseComparison() {
  auto left = ParsePrimary();
  static const std::unordered_map<TokenType, std::string> OPS = {
    {TokenType::EQ, "="}, {TokenType::LT, "<"}, {TokenType::GT, ">"},
    {TokenType::LEQ, "<="}, {TokenType::GEQ, ">="}, {TokenType::NEQ, "!="},
  };
  for (auto& [tt, op_str] : OPS) {
    if (Match(tt)) {
      auto right = ParsePrimary();
      auto node  = std::make_unique<Expr>();
      node->type  = ExprType::BINARY_OP;
      node->op    = op_str;
      node->left  = std::move(left);
      node->right = std::move(right);
      return node;
    }
  }
  return left;
}

std::unique_ptr<Expr> Parser::ParsePrimary() {
  // Parenthesised expression
  if (Match(TokenType::LPAREN)) {
    auto e = ParseExpr();
    Expect(TokenType::RPAREN, "Expected ')'");
    return e;
  }

  // Bare star: SELECT *
  if (Match(TokenType::STAR)) {
    auto e  = std::make_unique<Expr>();
    e->type = ExprType::STAR;
    return e;
  }

  // Integer literal
  if (Check(TokenType::INT_LITERAL)) {
    auto e     = std::make_unique<Expr>();
    e->type    = ExprType::INT_LITERAL;
    e->int_val = std::stoi(Consume().value);
    return e;
  }

  // String literal
  if (Check(TokenType::STRING_LITERAL)) {
    auto e    = std::make_unique<Expr>();
    e->type   = ExprType::STRING_LITERAL;
    e->str_val= Consume().value;
    return e;
  }

  // Aggregate function: COUNT(*), SUM(expr), etc.
  static const std::unordered_map<TokenType, std::string> AGG_NAMES = {
    {TokenType::COUNT, "COUNT"}, {TokenType::SUM, "SUM"},
    {TokenType::AVG,   "AVG"},   {TokenType::MIN, "MIN"},
    {TokenType::MAX,   "MAX"},
  };
  for (auto& [tt, name] : AGG_NAMES) {
    if (Check(tt)) {
      Consume();
      Expect(TokenType::LPAREN, "Expected '(' after " + name);
      auto e       = std::make_unique<Expr>();
      e->type      = ExprType::AGGREGATE;
      e->func_name = name;
      if (Match(TokenType::STAR)) {
        e->is_star_arg = true;
      } else {
        e->left = ParseExpr();
      }
      Expect(TokenType::RPAREN, "Expected ')'");
      return e;
    }
  }

  // Column ref: identifier or table.column
  if (Check(TokenType::IDENTIFIER)) {
    std::string name = Consume().value;
    if (Match(TokenType::DOT)) {
      // table.column
      auto e        = std::make_unique<Expr>();
      e->type       = ExprType::COLUMN_REF;
      e->table_alias= name;
      e->col_name   = Expect(TokenType::IDENTIFIER, "Expected column name after '.'").value;
      return e;
    }
    auto e      = std::make_unique<Expr>();
    e->type     = ExprType::COLUMN_REF;
    e->col_name = name;
    return e;
  }

  throw std::runtime_error("Unexpected token in expression: '" + Current().value + "'");
}
