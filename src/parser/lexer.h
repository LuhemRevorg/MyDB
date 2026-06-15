#pragma once
#include <string>
#include <vector>
#include "parser/token.h"

class Lexer {
 public:
  explicit Lexer(std::string input);
  std::vector<Token> Tokenize();

 private:
  Token NextToken();
  void  SkipWhitespace();
  Token ReadNumber();
  Token ReadString();
  Token ReadIdentifierOrKeyword();

  char        Peek() const;
  char        Advance();
  bool        IsAtEnd() const;

  std::string input_;
  size_t      pos_{0};
  int         line_{1};
};
