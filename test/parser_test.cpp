#include <gtest/gtest.h>
#include <filesystem>
#include "parser/lexer.h"
#include "parser/parser.h"
#include "database.h"

// ── Lexer ─────────────────────────────────────────────────────────────────────

TEST(LexerTest, BasicTokens) {
  Lexer lexer("SELECT * FROM users WHERE age > 30;");
  auto tokens = lexer.Tokenize();
  EXPECT_EQ(tokens[0].type, TokenType::SELECT);
  EXPECT_EQ(tokens[1].type, TokenType::STAR);
  EXPECT_EQ(tokens[2].type, TokenType::FROM);
  EXPECT_EQ(tokens[3].type, TokenType::IDENTIFIER);
  EXPECT_EQ(tokens[3].value, "users");
  EXPECT_EQ(tokens[4].type, TokenType::WHERE);
  EXPECT_EQ(tokens[5].type, TokenType::IDENTIFIER);
  EXPECT_EQ(tokens[6].type, TokenType::GT);
  EXPECT_EQ(tokens[7].type, TokenType::INT_LITERAL);
  EXPECT_EQ(tokens[7].value, "30");
}

TEST(LexerTest, StringLiteral) {
  Lexer lexer("INSERT INTO t VALUES ('hello', 42);");
  auto tokens = lexer.Tokenize();
  bool found_string = false;
  for (const auto& t : tokens)
    if (t.type == TokenType::STRING_LITERAL && t.value == "hello") found_string = true;
  EXPECT_TRUE(found_string);
}

TEST(LexerTest, CaseInsensitiveKeywords) {
  Lexer lexer("select * from Users");
  auto tokens = lexer.Tokenize();
  EXPECT_EQ(tokens[0].type, TokenType::SELECT);
  EXPECT_EQ(tokens[2].type, TokenType::FROM);
  EXPECT_EQ(tokens[3].type, TokenType::IDENTIFIER);
  EXPECT_EQ(tokens[3].value, "Users");
}

TEST(LexerTest, ComparisonOperators) {
  Lexer lexer("<= >= != <>");
  auto tokens = lexer.Tokenize();
  EXPECT_EQ(tokens[0].type, TokenType::LEQ);
  EXPECT_EQ(tokens[1].type, TokenType::GEQ);
  EXPECT_EQ(tokens[2].type, TokenType::NEQ);
  EXPECT_EQ(tokens[3].type, TokenType::NEQ);
}

// ── Parser ────────────────────────────────────────────────────────────────────

TEST(ParserTest, CreateTable) {
  Lexer  l("CREATE TABLE users (id INT, name VARCHAR(50), age INT);");
  Parser p(l.Tokenize());
  auto stmt = p.Parse();
  auto& ct = std::get<CreateTableStmt>(stmt);
  EXPECT_EQ(ct.table_name, "users");
  ASSERT_EQ(ct.columns.size(), 3u);
  EXPECT_EQ(ct.columns[0].name, "id");
  EXPECT_EQ(ct.columns[0].type_id, TypeId::INT);
  EXPECT_EQ(ct.columns[1].name, "name");
  EXPECT_EQ(ct.columns[1].type_id, TypeId::VARCHAR);
  EXPECT_EQ(ct.columns[1].max_len, 50u);
  EXPECT_EQ(ct.columns[2].name, "age");
}

TEST(ParserTest, Insert) {
  Lexer  l("INSERT INTO users VALUES (1, 'Alice', 28);");
  Parser p(l.Tokenize());
  auto stmt = p.Parse();
  auto& ins = std::get<InsertStmt>(stmt);
  EXPECT_EQ(ins.table_name, "users");
  ASSERT_EQ(ins.values.size(), 3u);
  EXPECT_EQ(ins.values[0]->type, ExprType::INT_LITERAL);
  EXPECT_EQ(ins.values[0]->int_val, 1);
  EXPECT_EQ(ins.values[1]->type, ExprType::STRING_LITERAL);
  EXPECT_EQ(ins.values[1]->str_val, "Alice");
}

TEST(ParserTest, SelectStar) {
  Lexer  l("SELECT * FROM users;");
  Parser p(l.Tokenize());
  auto stmt = p.Parse();
  auto& sel = std::get<SelectStmt>(stmt);
  EXPECT_EQ(sel.from_table, "users");
  ASSERT_EQ(sel.select_list.size(), 1u);
  EXPECT_EQ(sel.select_list[0].expr->type, ExprType::STAR);
  EXPECT_EQ(sel.where_clause, nullptr);
}

TEST(ParserTest, SelectWithWhere) {
  Lexer  l("SELECT name, age FROM users WHERE age >= 18 AND age < 65;");
  Parser p(l.Tokenize());
  auto stmt = p.Parse();
  auto& sel = std::get<SelectStmt>(stmt);
  EXPECT_EQ(sel.from_table, "users");
  ASSERT_EQ(sel.select_list.size(), 2u);
  ASSERT_NE(sel.where_clause, nullptr);
  EXPECT_EQ(sel.where_clause->op, "AND");
}

TEST(ParserTest, SelectWithAggregate) {
  Lexer  l("SELECT COUNT(*), SUM(age) FROM users;");
  Parser p(l.Tokenize());
  auto stmt = p.Parse();
  auto& sel = std::get<SelectStmt>(stmt);
  ASSERT_EQ(sel.select_list.size(), 2u);
  EXPECT_EQ(sel.select_list[0].expr->type, ExprType::AGGREGATE);
  EXPECT_EQ(sel.select_list[0].expr->func_name, "COUNT");
  EXPECT_TRUE(sel.select_list[0].expr->is_star_arg);
  EXPECT_EQ(sel.select_list[1].expr->func_name, "SUM");
}

// ── End-to-end via Database ───────────────────────────────────────────────────

class DatabaseTest : public ::testing::Test {
 protected:
  const std::string path_ = "/tmp/mydb_e2e_test.db";
  void TearDown() override { std::filesystem::remove(path_); }
};

TEST_F(DatabaseTest, CreateInsertSelect) {
  Database db{path_};
  db.Execute("CREATE TABLE users (id INT, name VARCHAR(50), age INT);");
  db.Execute("INSERT INTO users VALUES (1, 'Alice', 28);");
  db.Execute("INSERT INTO users VALUES (2, 'Bob', 34);");
  db.Execute("INSERT INTO users VALUES (3, 'Carol', 22);");

  auto result = db.Execute("SELECT * FROM users;");
  ASSERT_EQ(result.rows.size(), 3u);
  EXPECT_EQ(result.rows[0][1], "Alice");
  EXPECT_EQ(result.rows[1][1], "Bob");
  EXPECT_EQ(result.rows[2][1], "Carol");
}

TEST_F(DatabaseTest, SelectWithWhere) {
  Database db{path_};
  db.Execute("CREATE TABLE users (id INT, name VARCHAR(50), age INT);");
  db.Execute("INSERT INTO users VALUES (1, 'Alice', 28);");
  db.Execute("INSERT INTO users VALUES (2, 'Bob', 34);");
  db.Execute("INSERT INTO users VALUES (3, 'Carol', 22);");

  auto result = db.Execute("SELECT name FROM users WHERE age > 25;");
  ASSERT_EQ(result.rows.size(), 2u);
  EXPECT_EQ(result.rows[0][0], "Alice");
  EXPECT_EQ(result.rows[1][0], "Bob");
}

TEST_F(DatabaseTest, SelectWithAndCondition) {
  Database db{path_};
  db.Execute("CREATE TABLE products (id INT, price INT, stock INT);");
  db.Execute("INSERT INTO products VALUES (1, 100, 5);");
  db.Execute("INSERT INTO products VALUES (2, 200, 0);");
  db.Execute("INSERT INTO products VALUES (3, 150, 10);");

  auto result = db.Execute("SELECT id FROM products WHERE price > 90 AND stock > 0;");
  ASSERT_EQ(result.rows.size(), 2u);
  EXPECT_EQ(result.rows[0][0], "1");
  EXPECT_EQ(result.rows[1][0], "3");
}

TEST_F(DatabaseTest, DuplicateTableError) {
  Database db{path_};
  db.Execute("CREATE TABLE t (id INT);");
  EXPECT_THROW(db.Execute("CREATE TABLE t (id INT);"), std::runtime_error);
}

TEST_F(DatabaseTest, UnknownTableError) {
  Database db{path_};
  EXPECT_THROW(db.Execute("SELECT * FROM nonexistent;"), std::runtime_error);
}
