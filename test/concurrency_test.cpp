#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <set>
#include "concurrency/lockfree_hashtable.h"
#include "executor/thread_pool.h"
#include "executor/parallel_seq_scan.h"
#include "executor/parallel_hash_join.h"
#include "executor/parallel_sort.h"
#include "catalog/catalog.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"

// ── ThreadPool ────────────────────────────────────────────────────────────────

TEST(ThreadPoolTest, RunsAllTasks) {
  ThreadPool pool(4);
  std::atomic<int> count{0};
  for (int i = 0; i < 100; ++i)
    pool.Submit([&] { count.fetch_add(1, std::memory_order_relaxed); });
  pool.WaitAll();
  EXPECT_EQ(count.load(), 100);
}

TEST(ThreadPoolTest, SingleThread) {
  ThreadPool pool(1);
  std::vector<int> order;
  std::mutex mu;
  for (int i = 0; i < 10; ++i)
    pool.Submit([&, i] { std::scoped_lock lock(mu); order.push_back(i); });
  pool.WaitAll();
  EXPECT_EQ(order.size(), 10u);
}

// ── LockFreeHashTable ─────────────────────────────────────────────────────────

TEST(LockFreeHashTableTest, SingleThreadedInsertLookup) {
  Schema schema{{{"v", ColumnType::Int()}}};
  LockFreeHashTable ht(256);
  for (int i = 0; i < 100; ++i) {
    Tuple t({Value::MakeInt(i * 10)}, schema);
    ht.Insert(i, t);
  }
  for (int i = 0; i < 100; ++i) {
    std::vector<const Tuple*> results;
    ht.Lookup(i, results);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->GetValue(schema, 0).AsInt(), i * 10);
  }
}

TEST(LockFreeHashTableTest, ConcurrentInsert) {
  Schema schema{{{"v", ColumnType::Int()}}};
  LockFreeHashTable ht(1024);
  constexpr int N = 1000;

  // 4 threads each insert N/4 items.
  ThreadPool pool(4);
  for (int t = 0; t < 4; ++t) {
    pool.Submit([&, t] {
      for (int i = t * (N / 4); i < (t + 1) * (N / 4); ++i) {
        Tuple tuple({Value::MakeInt(i)}, schema);
        ht.Insert(i, tuple);
      }
    });
  }
  pool.WaitAll();

  // All N items should be findable.
  for (int i = 0; i < N; ++i) {
    std::vector<const Tuple*> results;
    ht.Lookup(i, results);
    EXPECT_EQ(results.size(), 1u) << "Missing key " << i;
  }
}

TEST(LockFreeHashTableTest, DuplicateKeys) {
  Schema schema{{{"v", ColumnType::Int()}}};
  LockFreeHashTable ht(64);
  for (int i = 0; i < 5; ++i) {
    Tuple t({Value::MakeInt(i)}, schema);
    ht.Insert(42, t);
  }
  std::vector<const Tuple*> results;
  ht.Lookup(42, results);
  EXPECT_EQ(results.size(), 5u);
}

// ── Parallel operators ────────────────────────────────────────────────────────

class ParallelOpTest : public ::testing::Test {
 protected:
  const std::string path_ = "/tmp/mydb_par_test.db";
  void TearDown() override { std::filesystem::remove(path_); }

  DiskManager       dm_{path_};
  BufferPoolManager bpm_{512, &dm_};
  Catalog           cat_{&bpm_};

  static constexpr int N = 2000;  // rows per table

  void SetUp() override {
    cat_.CreateTable("data", Schema{{{"id", ColumnType::Int()}, {"val", ColumnType::Int()}}});
    cat_.CreateTable("refs", Schema{{{"data_id", ColumnType::Int()}, {"score", ColumnType::Int()}}});

    Schema ds = cat_.GetTable("data")->schema;
    Schema rs = cat_.GetTable("refs")->schema;
    for (int i = 0; i < N; ++i) {
      Tuple t({Value::MakeInt(i), Value::MakeInt(i * 2)}, ds);
      RID rid; cat_.GetTable("data")->heap_file->InsertTuple(t, &rid);
    }
    // Half the data rows have a matching ref row
    for (int i = 0; i < N / 2; ++i) {
      Tuple t({Value::MakeInt(i), Value::MakeInt(i + 100)}, rs);
      RID rid; cat_.GetTable("refs")->heap_file->InsertTuple(t, &rid);
    }
  }
};

TEST_F(ParallelOpTest, ParallelScanCorrectness) {
  for (size_t n_threads : {1u, 2u, 4u}) {
    ParallelSeqScan scan(cat_.GetTable("data"), "data", n_threads);
    scan.Init();
    std::set<int> seen;
    Tuple t;
    while (scan.Next(&t)) {
      Schema& s = cat_.GetTable("data")->schema;
      seen.insert(t.GetValue(s, 0).AsInt());
    }
    scan.Close();
    EXPECT_EQ(seen.size(), static_cast<size_t>(N))
        << "n_threads=" << n_threads << " produced wrong count";
    EXPECT_EQ(*seen.begin(), 0);
    EXPECT_EQ(*seen.rbegin(), N - 1);
  }
}

TEST_F(ParallelOpTest, ParallelScanSameResultAsSerial) {
  // Serial scan
  std::vector<int> serial_ids;
  cat_.GetTable("data")->heap_file->Scan([&](const RID&, const Tuple& t) {
    serial_ids.push_back(t.GetValue(cat_.GetTable("data")->schema, 0).AsInt());
    return true;
  });
  std::sort(serial_ids.begin(), serial_ids.end());

  // Parallel scan
  ParallelSeqScan scan(cat_.GetTable("data"), "data", 4);
  scan.Init();
  std::vector<int> par_ids;
  Tuple t;
  while (scan.Next(&t))
    par_ids.push_back(t.GetValue(scan.GetOutputSchema(), 0).AsInt());
  scan.Close();
  std::sort(par_ids.begin(), par_ids.end());

  EXPECT_EQ(serial_ids, par_ids);
}

TEST_F(ParallelOpTest, ParallelHashJoinCorrectness) {
  for (size_t n_threads : {1u, 2u, 4u}) {
    auto left  = std::make_unique<ParallelSeqScan>(cat_.GetTable("data"), "data", n_threads);
    auto right = std::make_unique<ParallelSeqScan>(cat_.GetTable("refs"), "refs", n_threads);

    // Build key exprs: data.id = refs.data_id
    Expr left_key;
    left_key.type = ExprType::COLUMN_REF;
    left_key.col_name = "id";

    Expr right_key;
    right_key.type = ExprType::COLUMN_REF;
    right_key.col_name = "data_id";

    ParallelHashJoin join(std::move(left), std::move(right),
                         &left_key, &right_key, n_threads);
    join.Init();
    int count = 0;
    Tuple t;
    while (join.Next(&t)) ++count;
    join.Close();
    EXPECT_EQ(count, N / 2) << "n_threads=" << n_threads;
  }
}

TEST_F(ParallelOpTest, ParallelSortCorrectness) {
  ParallelSeqScan scan(cat_.GetTable("data"), "data", 4);

  Expr sort_expr;
  sort_expr.type = ExprType::COLUMN_REF;
  sort_expr.col_name = "id";

  OrderByItem order_item;
  order_item.expr = std::unique_ptr<Expr>(&sort_expr);
  order_item.ascending = true;

  ParallelSort sorter(
      std::make_unique<ParallelSeqScan>(cat_.GetTable("data"), "data", 4),
      std::vector<const OrderByItem*>{&order_item}, 4);

  sorter.Init();
  int prev = -1;
  Tuple t;
  int count = 0;
  while (sorter.Next(&t)) {
    int id = t.GetValue(sorter.GetOutputSchema(), 0).AsInt();
    EXPECT_GE(id, prev);
    prev = id;
    ++count;
  }
  sorter.Close();
  EXPECT_EQ(count, N);

  // Release the expr pointer before it gets double-freed
  order_item.expr.release();
}
