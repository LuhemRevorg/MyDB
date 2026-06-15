#include "catalog/catalog.h"

Catalog::Catalog(BufferPoolManager* bpm) : bpm_{bpm} {}

void Catalog::CreateTable(const std::string& name, Schema schema) {
  if (TableExists(name))
    throw std::runtime_error("Table already exists: " + name);
  TableInfo info{name, std::move(schema), nullptr, INVALID_PAGE_ID};
  info.heap_file = std::make_unique<HeapFile>(bpm_, info.schema);
  info.first_page_id = info.heap_file->GetFirstPageId();
  tables_.emplace(name, std::move(info));
}

TableInfo* Catalog::GetTable(const std::string& name) {
  auto it = tables_.find(name);
  if (it == tables_.end())
    throw std::runtime_error("Table not found: " + name);
  return &it->second;
}

bool Catalog::TableExists(const std::string& name) const {
  return tables_.count(name) > 0;
}
