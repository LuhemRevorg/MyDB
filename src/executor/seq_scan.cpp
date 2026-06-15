#include "executor/seq_scan.h"

// If an alias is given, prefix every column name ("users u" → "u.id", "u.name").
// This makes columns unambiguous after a join.
static Schema MakeOutputSchema(const Schema& base, const std::string& alias) {
  if (alias.empty()) return base;
  std::vector<Column> cols;
  for (size_t i = 0; i < base.GetColumnCount(); ++i) {
    Column c = base.GetColumn(i);
    c.name = alias + "." + c.name;
    cols.push_back(c);
  }
  return Schema{cols};
}

SeqScan::SeqScan(TableInfo* table_info, std::string alias)
    : table_info_{table_info},
      output_schema_{MakeOutputSchema(table_info->schema, alias)} {}

void SeqScan::Init() {
  buffer_.clear();
  cursor_ = 0;
  table_info_->heap_file->Scan([&](const RID&, const Tuple& t) {
    buffer_.push_back(t);
    return true;
  });
}

bool SeqScan::Next(Tuple* out) {
  if (cursor_ >= buffer_.size()) return false;
  *out = buffer_[cursor_++];
  return true;
}

void SeqScan::Close() { buffer_.clear(); }
