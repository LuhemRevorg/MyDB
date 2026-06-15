#include "storage/heap_file.h"
#include <cstring>
#include <stdexcept>

// Page layout:
//   [0..3]  num_tuples  (uint32_t)
//   [4..7]  next_page_id (page_id_t)
//   [8..]   tuple data, packed tightly

static constexpr int HEAP_HEADER_SIZE = 8;

static uint32_t& NumTuples(char* data) {
  return *reinterpret_cast<uint32_t*>(data);
}

static page_id_t& NextPageId(char* data) {
  return *reinterpret_cast<page_id_t*>(data + 4);
}

static char* TupleSlot(char* data, uint32_t slot, uint32_t tuple_size) {
  return data + HEAP_HEADER_SIZE + slot * tuple_size;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

uint32_t HeapFile::MaxTuplesPerPage() const {
  return (PAGE_SIZE - HEAP_HEADER_SIZE) / schema_.GetTupleSize();
}

static page_id_t AllocateHeapPage(BufferPoolManager* bpm) {
  page_id_t pid;
  Page* page = bpm->NewPage(&pid);
  if (!page) throw std::runtime_error("Buffer pool full — cannot allocate heap page");
  char* data = page->GetData();
  NumTuples(data)  = 0;
  NextPageId(data) = INVALID_PAGE_ID;
  bpm->UnpinPage(pid, true);
  return pid;
}

// ── Constructors ─────────────────────────────────────────────────────────────

HeapFile::HeapFile(BufferPoolManager* bpm, const Schema& schema)
    : bpm_{bpm}, schema_{schema} {
  first_page_id_ = AllocateHeapPage(bpm_);
  last_page_id_  = first_page_id_;
}

HeapFile::HeapFile(BufferPoolManager* bpm, const Schema& schema, page_id_t first_page_id)
    : bpm_{bpm}, schema_{schema}, first_page_id_{first_page_id} {
  // Walk the chain to find the last page.
  page_id_t cur = first_page_id_;
  while (true) {
    Page* page = bpm_->FetchPage(cur);
    page_id_t next = NextPageId(page->GetData());
    bpm_->UnpinPage(cur, false);
    if (next == INVALID_PAGE_ID) { last_page_id_ = cur; break; }
    cur = next;
  }
}

// ── InsertTuple ───────────────────────────────────────────────────────────────

bool HeapFile::InsertTuple(const Tuple& tuple, RID* rid) {
  Page* page = bpm_->FetchPage(last_page_id_);
  if (!page) return false;

  char* data = page->GetData();
  uint32_t max = MaxTuplesPerPage();

  if (NumTuples(data) >= max) {
    bpm_->UnpinPage(last_page_id_, false);

    // Current last page is full — allocate a new one and link it.
    page_id_t new_pid;
    try { new_pid = AllocateHeapPage(bpm_); }
    catch (...) { return false; }

    // Write next_page_id into the old last page.
    page = bpm_->FetchPage(last_page_id_);
    if (!page) return false;
    NextPageId(page->GetData()) = new_pid;
    bpm_->UnpinPage(last_page_id_, true);

    last_page_id_ = new_pid;
    page = bpm_->FetchPage(last_page_id_);
    if (!page) return false;
    data = page->GetData();
  }

  uint32_t slot = NumTuples(data);
  memcpy(TupleSlot(data, slot, schema_.GetTupleSize()), tuple.GetData(), schema_.GetTupleSize());
  NumTuples(data)++;

  if (rid) *rid = {last_page_id_, slot};
  bpm_->UnpinPage(last_page_id_, true);
  return true;
}

// ── Scan ─────────────────────────────────────────────────────────────────────

void HeapFile::Scan(std::function<bool(const RID&, const Tuple&)> callback) const {
  page_id_t cur = first_page_id_;
  uint32_t tuple_size = schema_.GetTupleSize();

  while (cur != INVALID_PAGE_ID) {
    Page* page = bpm_->FetchPage(cur);
    if (!page) break;

    char* data       = page->GetData();
    uint32_t n       = NumTuples(data);
    page_id_t next   = NextPageId(data);

    for (uint32_t slot = 0; slot < n; ++slot) {
      Tuple t = Tuple::FromBytes(TupleSlot(data, slot, tuple_size), tuple_size);
      if (!callback({cur, slot}, t)) {
        bpm_->UnpinPage(cur, false);
        return;
      }
    }

    bpm_->UnpinPage(cur, false);
    cur = next;
  }
}

std::vector<page_id_t> HeapFile::GetPageIds() const {
  std::vector<page_id_t> ids;
  page_id_t cur = first_page_id_;
  while (cur != INVALID_PAGE_ID) {
    ids.push_back(cur);
    Page* page = bpm_->FetchPage(cur);
    if (!page) break;
    page_id_t next = NextPageId(page->GetData());
    bpm_->UnpinPage(cur, false);
    cur = next;
  }
  return ids;
}

void HeapFile::ScanPage(page_id_t page_id,
                        std::function<void(const RID&, const Tuple&)> callback) const {
  Page* page = bpm_->FetchPage(page_id);
  if (!page) return;
  char* data = page->GetData();
  uint32_t n = NumTuples(data);
  uint32_t tuple_size = schema_.GetTupleSize();
  for (uint32_t slot = 0; slot < n; ++slot) {
    Tuple t = Tuple::FromBytes(TupleSlot(data, slot, tuple_size), tuple_size);
    callback({page_id, slot}, t);
  }
  bpm_->UnpinPage(page_id, false);
}
