#include "parser/lexer.h"
#include <cctype>
#include <stdexcept>
#include <unordered_map>

static const std::unordered_map<std::string, TokenType> KEYWORDS = {
  {"CREATE",  TokenType::CREATE},  {"TABLE",   TokenType::TABLE},
  {"INSERT",  TokenType::INSERT},  {"INTO",    TokenType::INTO},
  {"VALUES",  TokenType::VALUES},  {"SELECT",  TokenType::SELECT},
  {"FROM",    TokenType::FROM},    {"WHERE",   TokenType::WHERE},
  {"AS",      TokenType::AS},      {"JOIN",    TokenType::JOIN},
  {"ON",      TokenType::ON},      {"AND",     TokenType::AND},
  {"OR",      TokenType::OR},      {"NOT",     TokenType::NOT},
  {"GROUP",   TokenType::GROUP},   {"BY",      TokenType::BY},
  {"ORDER",   TokenType::ORDER},   {"ASC",     TokenType::ASC},
  {"DESC",    TokenType::DESC},    {"EXPLAIN", TokenType::EXPLAIN},
  {"INT",     TokenType::INT_KW},  {"VARCHAR", TokenType::VARCHAR_KW},
  {"COUNT",   TokenType::COUNT},   {"SUM",     TokenType::SUM},
  {"AVG",     TokenType::AVG},     {"MIN",     TokenType::MIN},
  {"MAX",     TokenType::MAX},
};

Lexer::Lexer(std::string input) : input_{std::move(input)} {}

std::vector<Token> Lexer::Tokenize() {
  std::vector<Token> tokens;
  while (true) {
    Token t = NextToken();
    tokens.push_back(t);
    if (t.type == TokenType::EOF_TOKEN) break;
  }
  return tokens;
}

bool  Lexer::IsAtEnd()   const { return pos_ >= input_.size(); }
char  Lexer::Peek()      const { return IsAtEnd() ? '\0' : input_[pos_]; }
char  Lexer::Advance()         { return input_[pos_++]; }

void Lexer::SkipWhitespace() {
  while (!IsAtEnd()) {
    char c = Peek();
    if (c == '\n') { ++line_; ++pos_; }
    else if (std::isspace(static_cast<unsigned char>(c))) ++pos_;
    else if (c == '-' && pos_ + 1 < input_.size() && input_[pos_+1] == '-') {
      while (!IsAtEnd() && Peek() != '\n') ++pos_;  // skip line comment
    }
    else break;
  }
}

Token Lexer::ReadNumber() {
  size_t start = pos_;
  while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) Advance();
  return {TokenType::INT_LITERAL, input_.substr(start, pos_ - start), line_};
}

Token Lexer::ReadString() {
  Advance();  // consume opening '
  size_t start = pos_;
  while (!IsAtEnd() && Peek() != '\'') {
    if (Peek() == '\n') ++line_;
    Advance();
  }
  if (IsAtEnd()) throw std::runtime_error("Unterminated string literal");
  std::string val = input_.substr(start, pos_ - start);
  Advance();  // consume closing '
  return {TokenType::STRING_LITERAL, val, line_};
}

Token Lexer::ReadIdentifierOrKeyword() {
  size_t start = pos_;
  while (!IsAtEnd() && (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_'))
    Advance();
  std::string raw = input_.substr(start, pos_ - start);
  // Case-insensitive keyword match
  std::string upper;
  for (char c : raw) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  auto it = KEYWORDS.find(upper);
  if (it != KEYWORDS.end()) return {it->second, upper, line_};
  return {TokenType::IDENTIFIER, raw, line_};
}

Token Lexer::NextToken() {
  SkipWhitespace();
  if (IsAtEnd()) return {TokenType::EOF_TOKEN, "", line_};

  char c = Peek();

  if (std::isdigit(static_cast<unsigned char>(c)))  return ReadNumber();
  if (c == '\'')                                     return ReadString();
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return ReadIdentifierOrKeyword();

  Advance();
  switch (c) {
    case '(': return {TokenType::LPAREN,    "(", line_};
    case ')': return {TokenType::RPAREN,    ")", line_};
    case ',': return {TokenType::COMMA,     ",", line_};
    case ';': return {TokenType::SEMICOLON, ";", line_};
    case '.': return {TokenType::DOT,       ".", line_};
    case '*': return {TokenType::STAR,      "*", line_};
    case '=': return {TokenType::EQ,        "=", line_};
    case '<':
      if (!IsAtEnd() && Peek() == '=') { Advance(); return {TokenType::LEQ, "<=", line_}; }
      if (!IsAtEnd() && Peek() == '>') { Advance(); return {TokenType::NEQ, "<>", line_}; }
      return {TokenType::LT, "<", line_};
    case '>':
      if (!IsAtEnd() && Peek() == '=') { Advance(); return {TokenType::GEQ, ">=", line_}; }
      return {TokenType::GT, ">", line_};
    case '!':
      if (!IsAtEnd() && Peek() == '=') { Advance(); return {TokenType::NEQ, "!=", line_}; }
      throw std::runtime_error(std::string("Unexpected character: !"));
    default:
      throw std::runtime_error(std::string("Unexpected character: ") + c);
  }
}
