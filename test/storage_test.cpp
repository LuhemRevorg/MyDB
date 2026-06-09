#include <gtest/gtest.h>
#include <cstring>
#include <filesystem>
#include "storage/disk_manager.h"
#include "storage/lru_replacer.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "common/types.h"

// ── DiskManager ───────────────────────────────────────────────────────────────

class DiskManagerTest : public ::testing::Test {
 protected:
  const std::string path_ = "/tmp/mydb_test.db";
  void TearDown() override { std::filesystem::remove(path_); }
};

TEST_F(DiskManagerTest, CreateNewFile) {
  DiskManager dm(path_);
  EXPECT_EQ(dm.GetNumPages(), 0);
  EXPECT_TRUE(std::filesystem::exists(path_));
}

TEST_F(DiskManagerTest, AllocateAndReadWrite) {
  DiskManager dm(path_);

  page_id_t pid = dm.AllocatePage();
  EXPECT_EQ(pid, 0);
  EXPECT_EQ(dm.GetNumPages(), 1);

  char write_buf[PAGE_SIZE]{};
  memset(write_buf, 0xAB, PAGE_SIZE);
  dm.WritePage(pid, write_buf);

  char read_buf[PAGE_SIZE]{};
  dm.ReadPage(pid, read_buf);
  EXPECT_EQ(memcmp(write_buf, read_buf, PAGE_SIZE), 0);
}

TEST_F(DiskManagerTest, MultiplePages) {
  DiskManager dm(path_);
  constexpr int N = 5;

  for (int i = 0; i < N; ++i) {
    page_id_t pid = dm.AllocatePage();
    char buf[PAGE_SIZE]{};
    memset(buf, static_cast<char>(i + 1), PAGE_SIZE);
    dm.WritePage(pid, buf);
  }
  EXPECT_EQ(dm.GetNumPages(), N);

  for (int i = 0; i < N; ++i) {
    char buf[PAGE_SIZE]{};
    dm.ReadPage(i, buf);
    EXPECT_EQ(static_cast<unsigned char>(buf[0]), i + 1);
  }
}

TEST_F(DiskManagerTest, PersistsAcrossReopen) {
  char write_buf[PAGE_SIZE]{};
  memset(write_buf, 0x42, PAGE_SIZE);
  {
    DiskManager dm(path_);
    page_id_t pid = dm.AllocatePage();
    dm.WritePage(pid, write_buf);
  }
  // Reopen — should find 1 page
  DiskManager dm2(path_);
  EXPECT_EQ(dm2.GetNumPages(), 1);
  char read_buf[PAGE_SIZE]{};
  dm2.ReadPage(0, read_buf);
  EXPECT_EQ(memcmp(write_buf, read_buf, PAGE_SIZE), 0);
}

// ── LRUReplacer ───────────────────────────────────────────────────────────────

TEST(LRUReplacerTest, EvictLRUOrder) {
  LRUReplacer lru(5);
  lru.RecordAccess(1);
  lru.RecordAccess(2);
  lru.RecordAccess(3);
  lru.SetEvictable(1, true);
  lru.SetEvictable(2, true);
  lru.SetEvictable(3, true);

  frame_id_t fid;
  ASSERT_TRUE(lru.Evict(&fid));
  EXPECT_EQ(fid, 1);  // least recently used
  ASSERT_TRUE(lru.Evict(&fid));
  EXPECT_EQ(fid, 2);
  ASSERT_TRUE(lru.Evict(&fid));
  EXPECT_EQ(fid, 3);
  EXPECT_FALSE(lru.Evict(&fid));
}

TEST(LRUReplacerTest, PinnedFrameNotEvicted) {
  LRUReplacer lru(3);
  lru.RecordAccess(0);
  lru.RecordAccess(1);
  lru.SetEvictable(0, true);
  // frame 1 is not evictable (pinned)

  frame_id_t fid;
  ASSERT_TRUE(lru.Evict(&fid));
  EXPECT_EQ(fid, 0);
  EXPECT_FALSE(lru.Evict(&fid));
  EXPECT_EQ(lru.Size(), 0);
}

TEST(LRUReplacerTest, RecordAccessUpdatesOrder) {
  LRUReplacer lru(3);
  lru.RecordAccess(1);
  lru.RecordAccess(2);
  lru.RecordAccess(1);  // 1 is now MRU; 2 is LRU
  lru.SetEvictable(1, true);
  lru.SetEvictable(2, true);

  frame_id_t fid;
  ASSERT_TRUE(lru.Evict(&fid));
  EXPECT_EQ(fid, 2);
}

TEST(LRUReplacerTest, Remove) {
  LRUReplacer lru(3);
  lru.RecordAccess(0);
  lru.SetEvictable(0, true);
  EXPECT_EQ(lru.Size(), 1);
  lru.Remove(0);
  EXPECT_EQ(lru.Size(), 0);
  frame_id_t fid;
  EXPECT_FALSE(lru.Evict(&fid));
}

// ── BufferPoolManager ─────────────────────────────────────────────────────────

class BufferPoolTest : public ::testing::Test {
 protected:
  const std::string path_ = "/tmp/mydb_bpm_test.db";
  void TearDown() override { std::filesystem::remove(path_); }
};

TEST_F(BufferPoolTest, NewAndFetch) {
  DiskManager dm(path_);
  BufferPoolManager bpm(10, &dm);

  page_id_t pid;
  Page* page = bpm.NewPage(&pid);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(pid, 0);

  memset(page->GetData(), 0xCD, PAGE_SIZE);
  bpm.UnpinPage(pid, true);

  Page* fetched = bpm.FetchPage(pid);
  ASSERT_NE(fetched, nullptr);
  EXPECT_EQ(static_cast<unsigned char>(fetched->GetData()[0]), 0xCD);
  bpm.UnpinPage(pid, false);
}

TEST_F(BufferPoolTest, EvictionWhenFull) {
  DiskManager dm(path_);
  constexpr size_t POOL = 3;
  BufferPoolManager bpm(POOL, &dm);

  page_id_t pids[POOL];
  for (size_t i = 0; i < POOL; ++i) {
    Page* p = bpm.NewPage(&pids[i]);
    ASSERT_NE(p, nullptr);
    bpm.UnpinPage(pids[i], false);
  }

  // Pool is full but all pages are unpinned — allocating one more should evict one.
  page_id_t new_pid;
  Page* p = bpm.NewPage(&new_pid);
  EXPECT_NE(p, nullptr);
  bpm.UnpinPage(new_pid, false);
}

TEST_F(BufferPoolTest, CannotEvictPinnedPages) {
  DiskManager dm(path_);
  constexpr size_t POOL = 2;
  BufferPoolManager bpm(POOL, &dm);

  page_id_t p0, p1;
  bpm.NewPage(&p0);  // pinned (not unpinned)
  bpm.NewPage(&p1);  // pinned

  page_id_t p2;
  Page* p = bpm.NewPage(&p2);
  EXPECT_EQ(p, nullptr);  // pool full, nothing evictable

  bpm.UnpinPage(p0, false);
  bpm.UnpinPage(p1, false);
}

TEST_F(BufferPoolTest, FlushPage) {
  DiskManager dm(path_);
  BufferPoolManager bpm(5, &dm);

  page_id_t pid;
  Page* page = bpm.NewPage(&pid);
  memset(page->GetData(), 0x77, PAGE_SIZE);
  bpm.UnpinPage(pid, true);
  bpm.FlushPage(pid);

  char buf[PAGE_SIZE]{};
  dm.ReadPage(pid, buf);
  EXPECT_EQ(static_cast<unsigned char>(buf[0]), 0x77);
}

// ── HeapFile ─────────────────────────────────────────────────────────────────

class HeapFileTest : public ::testing::Test {
 protected:
  const std::string path_ = "/tmp/mydb_heap_test.db";
  Schema schema_{{{"id", ColumnType::Int()}, {"age", ColumnType::Int()}}};
  void TearDown() override { std::filesystem::remove(path_); }
};

TEST_F(HeapFileTest, InsertAndScan) {
  DiskManager dm(path_);
  BufferPoolManager bpm(20, &dm);
  HeapFile hf(&bpm, schema_);

  for (int i = 0; i < 10; ++i) {
    Tuple t({Value::MakeInt(i), Value::MakeInt(i * 2)}, schema_);
    RID rid;
    ASSERT_TRUE(hf.InsertTuple(t, &rid));
  }

  int count = 0;
  hf.Scan([&](const RID&, const Tuple& t) {
    EXPECT_EQ(t.GetValue(schema_, 0).AsInt(), count);
    EXPECT_EQ(t.GetValue(schema_, 1).AsInt(), count * 2);
    ++count;
    return true;
  });
  EXPECT_EQ(count, 10);
}

TEST_F(HeapFileTest, SpansMultiplePages) {
  DiskManager dm(path_);
  BufferPoolManager bpm(50, &dm);
  HeapFile hf(&bpm, schema_);

  // Each tuple is 8 bytes; max per page = (4096-8)/8 = 511
  // Insert 1500 to guarantee multiple pages.
  constexpr int N = 1500;
  for (int i = 0; i < N; ++i) {
    Tuple t({Value::MakeInt(i), Value::MakeInt(i)}, schema_);
    RID rid;
    ASSERT_TRUE(hf.InsertTuple(t, &rid));
  }

  int count = 0;
  hf.Scan([&](const RID&, const Tuple&) { ++count; return true; });
  EXPECT_EQ(count, N);
}

TEST_F(HeapFileTest, EarlyScanTermination) {
  DiskManager dm(path_);
  BufferPoolManager bpm(20, &dm);
  HeapFile hf(&bpm, schema_);

  for (int i = 0; i < 10; ++i) {
    Tuple t({Value::MakeInt(i), Value::MakeInt(i)}, schema_);
    RID rid;
    hf.InsertTuple(t, &rid);
  }

  int count = 0;
  hf.Scan([&](const RID&, const Tuple&) {
    return ++count < 5;  // stop after 5
  });
  EXPECT_EQ(count, 5);
}
