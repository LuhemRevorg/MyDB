#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include "common/types.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"

struct TableInfo {
  std::string              name;
  Schema                   schema;
  std::unique_ptr<HeapFile> heap_file;
  page_id_t                first_page_id{INVALID_PAGE_ID};
};

class Catalog {
 public:
  explicit Catalog(BufferPoolManager* bpm);

  void        CreateTable(const std::string& name, Schema schema);
  TableInfo*  GetTable(const std::string& name);
  bool        TableExists(const std::string& name) const;

 private:
  BufferPoolManager*                               bpm_;
  std::unordered_map<std::string, TableInfo>       tables_;
};
