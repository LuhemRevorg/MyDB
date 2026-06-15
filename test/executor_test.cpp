#include <gtest/gtest.h>
#include <filesystem>
#include "database.h"

class ExecTest : public ::testing::Test {
 protected:
  const std::string path_ = "/tmp/mydb_exec_test.db";
  void TearDown() override { std::filesystem::remove(path_); }

  Database db_{path_};

  void SetUp() override {
    db_.Execute("CREATE TABLE users (id INT, name VARCHAR(50), age INT);");
    db_.Execute("INSERT INTO users VALUES (1, 'Alice', 28);");
    db_.Execute("INSERT INTO users VALUES (2, 'Bob', 34);");
    db_.Execute("INSERT INTO users VALUES (3, 'Carol', 22);");
    db_.Execute("INSERT INTO users VALUES (4, 'Dave', 34);");

    db_.Execute("CREATE TABLE orders (id INT, user_id INT, amount INT);");
    db_.Execute("INSERT INTO orders VALUES (1, 1, 100);");
    db_.Execute("INSERT INTO orders VALUES (2, 1, 200);");
    db_.Execute("INSERT INTO orders VALUES (3, 2, 150);");
    db_.Execute("INSERT INTO orders VALUES (4, 3, 80);");
  }
};

// ── SeqScan + Filter ─────────────────────────────────────────────────────────

TEST_F(ExecTest, SelectStar) {
  auto r = db_.Execute("SELECT * FROM users;");
  EXPECT_EQ(r.rows.size(), 4u);
  EXPECT_EQ(r.columns.size(), 3u);
}

TEST_F(ExecTest, FilterEq) {
  auto r = db_.Execute("SELECT name FROM users WHERE age = 34;");
  ASSERT_EQ(r.rows.size(), 2u);
  EXPECT_EQ(r.rows[0][0], "Bob");
  EXPECT_EQ(r.rows[1][0], "Dave");
}

TEST_F(ExecTest, FilterAnd) {
  auto r = db_.Execute("SELECT name FROM users WHERE age > 25 AND age < 35;");
  ASSERT_EQ(r.rows.size(), 3u);
}

// ── Projection ───────────────────────────────────────────────────────────────

TEST_F(ExecTest, Projection) {
  auto r = db_.Execute("SELECT name, age FROM users WHERE age > 25;");
  ASSERT_EQ(r.columns.size(), 2u);
  EXPECT_EQ(r.columns[0], "name");
  EXPECT_EQ(r.columns[1], "age");
  EXPECT_EQ(r.rows.size(), 3u);
}

// ── Sort ─────────────────────────────────────────────────────────────────────

TEST_F(ExecTest, OrderByAsc) {
  auto r = db_.Execute("SELECT name FROM users ORDER BY age ASC;");
  ASSERT_EQ(r.rows.size(), 4u);
  EXPECT_EQ(r.rows[0][0], "Carol");   // age 22
  EXPECT_EQ(r.rows[1][0], "Alice");   // age 28
}

TEST_F(ExecTest, OrderByDesc) {
  auto r = db_.Execute("SELECT name FROM users ORDER BY age DESC;");
  ASSERT_EQ(r.rows.size(), 4u);
  EXPECT_EQ(r.rows[0][0], "Bob");    // age 34 (or Dave — same age, order undefined)
}

// ── HashAggregate ─────────────────────────────────────────────────────────────

TEST_F(ExecTest, CountStar) {
  auto r = db_.Execute("SELECT COUNT(*) FROM users;");
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0], "4");
}

TEST_F(ExecTest, GroupByCount) {
  auto r = db_.Execute("SELECT age, COUNT(*) FROM users GROUP BY age ORDER BY age ASC;");
  ASSERT_EQ(r.rows.size(), 3u);  // age 22, 28, 34
  EXPECT_EQ(r.rows[0][0], "22"); EXPECT_EQ(r.rows[0][1], "1");
  EXPECT_EQ(r.rows[1][0], "28"); EXPECT_EQ(r.rows[1][1], "1");
  EXPECT_EQ(r.rows[2][0], "34"); EXPECT_EQ(r.rows[2][1], "2");
}

TEST_F(ExecTest, SumAggregate) {
  auto r = db_.Execute("SELECT SUM(amount) FROM orders;");
  ASSERT_EQ(r.rows.size(), 1u);
  EXPECT_EQ(r.rows[0][0], "530");
}

// ── HashJoin ─────────────────────────────────────────────────────────────────

TEST_F(ExecTest, HashJoin) {
  auto r = db_.Execute(
      "SELECT u.name, o.amount "
      "FROM users u JOIN orders o ON u.id = o.user_id "
      "ORDER BY o.amount ASC;");
  ASSERT_EQ(r.rows.size(), 4u);
  EXPECT_EQ(r.rows[0][0], "Carol");  // amount 80
  EXPECT_EQ(r.rows[1][0], "Alice");  // amount 100
}

TEST_F(ExecTest, JoinWithGroupBy) {
  auto r = db_.Execute(
      "SELECT u.name, SUM(o.amount) "
      "FROM users u JOIN orders o ON u.id = o.user_id "
      "GROUP BY u.name "
      "ORDER BY u.name ASC;");
  ASSERT_EQ(r.rows.size(), 3u);
  EXPECT_EQ(r.rows[0][0], "Alice");
  EXPECT_EQ(r.rows[0][1], "300");  // 100 + 200
}

// ── Explain ───────────────────────────────────────────────────────────────────

TEST_F(ExecTest, Explain) {
  auto r = db_.Execute("EXPLAIN SELECT name FROM users WHERE age > 25;");
  ASSERT_FALSE(r.rows.empty());
  // Should mention Filter and SeqScan
  std::string full;
  for (const auto& row : r.rows) full += row[0];
  EXPECT_NE(full.find("Filter"), std::string::npos);
  EXPECT_NE(full.find("SeqScan"), std::string::npos);
}
