#pragma once
#include <fstream>
#include <mutex>
#include <string>
#include "common/config.h"

// Reads and writes fixed-size (PAGE_SIZE) pages to a binary file.
// Page N lives at byte offset N * PAGE_SIZE in the file.
// AllocatePage() extends the file by one page and returns the new page_id.
class DiskManager {
 public:
  explicit DiskManager(const std::string& db_file);
  ~DiskManager();

  DiskManager(const DiskManager&)            = delete;
  DiskManager& operator=(const DiskManager&) = delete;

  void      ReadPage(page_id_t page_id, char* page_data);
  void      WritePage(page_id_t page_id, const char* page_data);
  page_id_t AllocatePage();

  int GetNumPages() const { return num_pages_; }

 private:
  std::string  file_name_;
  std::fstream db_io_;
  int          num_pages_{0};
  std::mutex   io_latch_;
};
