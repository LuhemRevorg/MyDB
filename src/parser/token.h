#pragma once
#include <string>

enum class TokenType {
  // Keywords
  CREATE, TABLE, INSERT, INTO, VALUES,
  SELECT, FROM, WHERE, AS,
  JOIN, ON, AND, OR, NOT,
  GROUP, BY, ORDER, ASC, DESC,
  EXPLAIN,
  // Type keywords
  INT_KW, VARCHAR_KW,
  // Aggregate functions
  COUNT, SUM, AVG, MIN, MAX,
  // Literals
  INT_LITERAL, STRING_LITERAL,
  // Identifiers
  IDENTIFIER,
  // Punctuation
  LPAREN, RPAREN, COMMA, SEMICOLON, DOT, STAR,
  // Comparison operators
  EQ, LT, GT, LEQ, GEQ, NEQ,
  // Special
  EOF_TOKEN,
};

struct Token {
  TokenType   type;
  std::string value;  // raw text from source
  int         line{1};
};
