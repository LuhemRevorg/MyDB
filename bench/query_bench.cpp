#include <benchmark/benchmark.h>
#include <filesystem>
#include "catalog/catalog.h"
#include "executor/seq_scan.h"
#include "executor/hash_join.h"
#include "executor/sort.h"
#include "executor/parallel_seq_scan.h"
#include "executor/parallel_hash_join.h"
#include "executor/parallel_sort.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"

static constexpr int ROWS = 100000;

// ── Fixture: shared DB state loaded once ─────────────────────────────────────

struct BenchDB {
  static BenchDB& Get() { static BenchDB db; return db; }

  DiskManager       dm{"bench.db"};
  BufferPoolManager bpm{1024, &dm};
  Catalog           cat{&bpm};

  BenchDB() {
    Schema s{{{"id", ColumnType::Int()}, {"val", ColumnType::Int()}}};
    cat.CreateTable("big", s);
    cat.CreateTable("small", s);
    auto& big   = *cat.GetTable("big")->heap_file;
    auto& small = *cat.GetTable("small")->heap_file;
    for (int i = 0; i < ROWS; ++i) {
      Tuple t({Value::MakeInt(i), Value::MakeInt(i)}, s);
      RID r; big.InsertTuple(t, &r);
    }
    for (int i = 0; i < ROWS / 10; ++i) {
      Tuple t({Value::MakeInt(i), Value::MakeInt(i)}, s);
      RID r; small.InsertTuple(t, &r);
    }
  }
  ~BenchDB() { std::filesystem::remove("bench.db"); }
};

// ── Sequential scan ───────────────────────────────────────────────────────────

static void BM_SeqScan_Serial(benchmark::State& state) {
  auto& db = BenchDB::Get();
  for (auto _ : state) {
    SeqScan scan(db.cat.GetTable("big"));
    scan.Init();
    Tuple t; int count = 0;
    while (scan.Next(&t)) ++count;
    scan.Close();
    benchmark::DoNotOptimize(count);
  }
}
BENCHMARK(BM_SeqScan_Serial);

static void BM_SeqScan_Parallel(benchmark::State& state) {
  auto& db = BenchDB::Get();
  int n_threads = state.range(0);
  for (auto _ : state) {
    ParallelSeqScan scan(db.cat.GetTable("big"), "big", n_threads);
    scan.Init();
    Tuple t; int count = 0;
    while (scan.Next(&t)) ++count;
    scan.Close();
    benchmark::DoNotOptimize(count);
  }
}
BENCHMARK(BM_SeqScan_Parallel)->Arg(1)->Arg(2)->Arg(4)->Arg(8);

// ── Sort ──────────────────────────────────────────────────────────────────────

static Expr& SortExpr() {
  static Expr e; e.type = ExprType::COLUMN_REF; e.col_name = "val"; return e;
}
static OrderByItem& SortItem() {
  static OrderByItem item;
  item.ascending = true;
  item.expr.reset();  // non-owning in benchmarks — careful with lifetime
  return item;
}

static void BM_Sort_Serial(benchmark::State& state) {
  auto& db = BenchDB::Get();
  OrderByItem item; item.ascending = true;
  Expr e; e.type = ExprType::COLUMN_REF; e.col_name = "val";
  item.expr = std::unique_ptr<Expr>(&e);  // non-owning trick
  for (auto _ : state) {
    Sort sorter(std::make_unique<SeqScan>(db.cat.GetTable("big")),
                std::vector<const OrderByItem*>{&item});
    sorter.Init(); Tuple t; int count = 0;
    while (sorter.Next(&t)) ++count;
    sorter.Close();
    benchmark::DoNotOptimize(count);
  }
  item.expr.release();
}
BENCHMARK(BM_Sort_Serial);

static void BM_Sort_Parallel(benchmark::State& state) {
  auto& db = BenchDB::Get();
  int n = state.range(0);
  Expr e; e.type = ExprType::COLUMN_REF; e.col_name = "val";
  OrderByItem item; item.ascending = true;
  item.expr = std::unique_ptr<Expr>(&e);
  for (auto _ : state) {
    ParallelSort sorter(
        std::make_unique<ParallelSeqScan>(db.cat.GetTable("big"), "big", n),
        std::vector<const OrderByItem*>{&item}, n);
    sorter.Init(); Tuple t; int count = 0;
    while (sorter.Next(&t)) ++count;
    sorter.Close();
    benchmark::DoNotOptimize(count);
  }
  item.expr.release();
}
BENCHMARK(BM_Sort_Parallel)->Arg(1)->Arg(2)->Arg(4)->Arg(8);

BENCHMARK_MAIN();
