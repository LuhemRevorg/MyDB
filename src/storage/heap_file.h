#pragma once
#include <functional>
#include "common/types.h"
#include "storage/buffer_pool.h"

class HeapFile {
 public:
  // Create a new heap file — allocates the first page.
  HeapFile(BufferPoolManager* bpm, const Schema& schema);
  // Open an existing heap file from a known first page.
  HeapFile(BufferPoolManager* bpm, const Schema& schema, page_id_t first_page_id);

  // Insert a tuple. Sets *rid to the physical location. Returns false if pool is full.
  bool InsertTuple(const Tuple& tuple, RID* rid);

  // Scan all tuples. Callback returns false to stop early.
  void Scan(std::function<bool(const RID&, const Tuple&)> callback) const;

  // For parallel scan: returns the ordered list of page IDs in the chain.
  std::vector<page_id_t> GetPageIds() const;

  // Scan a single page. Used by parallel workers claiming page ranges.
  void ScanPage(page_id_t page_id,
                std::function<void(const RID&, const Tuple&)> callback) const;

  page_id_t        GetFirstPageId() const { return first_page_id_; }
  BufferPoolManager* GetBPM()       const { return bpm_; }
  const Schema&    GetSchema()      const { return schema_; }

 private:
  uint32_t MaxTuplesPerPage() const;

  BufferPoolManager* bpm_;
  Schema             schema_;
  page_id_t          first_page_id_{INVALID_PAGE_ID};
  page_id_t          last_page_id_{INVALID_PAGE_ID};
};
