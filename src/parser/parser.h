#pragma once
#include <vector>
#include "parser/ast.h"
#include "parser/token.h"

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens);
  Statement Parse();

 private:
  // Statement parsers
  CreateTableStmt ParseCreateTable();
  InsertStmt      ParseInsert();
  SelectStmt      ParseSelect();
  ExplainStmt     ParseExplain();

  // Clause parsers
  std::vector<SelectItem> ParseSelectList();
  SelectItem              ParseSelectItem();
  std::vector<JoinClause> ParseJoins();
  std::unique_ptr<Expr>   ParseWhere();
  std::vector<std::unique_ptr<Expr>> ParseGroupBy();
  std::vector<OrderByItem>           ParseOrderBy();

  // Expression parsers (precedence climbing)
  std::unique_ptr<Expr> ParseExpr();
  std::unique_ptr<Expr> ParseOr();
  std::unique_ptr<Expr> ParseAnd();
  std::unique_ptr<Expr> ParseNot();
  std::unique_ptr<Expr> ParseComparison();
  std::unique_ptr<Expr> ParsePrimary();

  // Token helpers
  Token&      Current();
  Token&      Peek(int offset = 1);
  Token       Consume();
  Token       Expect(TokenType type, const std::string& msg);
  bool        Check(TokenType type) const;
  bool        Match(TokenType type);

  std::vector<Token> tokens_;
  size_t             pos_{0};
};
