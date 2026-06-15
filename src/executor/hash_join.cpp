#include "executor/hash_join.h"
#include <cstring>

static Schema ConcatSchemas(const Schema& l, const Schema& r) {
  std::vector<Column> cols;
  for (size_t i = 0; i < l.GetColumnCount(); ++i) cols.push_back(l.GetColumn(i));
  for (size_t i = 0; i < r.GetColumnCount(); ++i) cols.push_back(r.GetColumn(i));
  return Schema{cols};
}

HashJoin::HashJoin(std::unique_ptr<Operator> left,
                   std::unique_ptr<Operator> right,
                   const Expr* left_key,
                   const Expr* right_key)
    : left_{std::move(left)},
      right_{std::move(right)},
      left_key_{left_key},
      right_key_{right_key},
      output_schema_{ConcatSchemas(left_->GetOutputSchema(), right_->GetOutputSchema())} {}

Tuple HashJoin::Concat(const Tuple& l, const Tuple& r,
                       const Schema& ls, const Schema& rs) {
  uint32_t total = ls.GetTupleSize() + rs.GetTupleSize();
  std::vector<char> buf(total);
  memcpy(buf.data(),                   l.GetData(), ls.GetTupleSize());
  memcpy(buf.data() + ls.GetTupleSize(), r.GetData(), rs.GetTupleSize());
  return Tuple::FromBytes(buf.data(), total);
}

void HashJoin::Init() {
  hash_table_.clear();
  right_buffer_.clear();
  right_cursor_ = match_cursor_ = 0;

  // Build phase: load left child into hash table keyed by left_key.
  left_->Init();
  Tuple t;
  while (left_->Next(&t)) {
    std::string key = EvalExpr(left_key_, t, left_->GetOutputSchema()).ToString();
    hash_table_[key].push_back(t);
  }
  left_->Close();

  // Buffer all right tuples (probe phase runs lazily in Next()).
  right_->Init();
  while (right_->Next(&t)) right_buffer_.push_back(t);
  right_->Close();
}

bool HashJoin::Next(Tuple* out) {
  while (true) {
    // Drain current bucket match first.
    if (!current_key_.empty()) {
      auto it = hash_table_.find(current_key_);
      if (it != hash_table_.end() && match_cursor_ < it->second.size()) {
        *out = Concat(it->second[match_cursor_++],
                      right_buffer_[right_cursor_ - 1],
                      left_->GetOutputSchema(),
                      right_->GetOutputSchema());
        return true;
      }
      current_key_.clear();
      match_cursor_ = 0;
    }

    // Advance to next right tuple.
    if (right_cursor_ >= right_buffer_.size()) return false;
    const Tuple& right_t = right_buffer_[right_cursor_++];
    current_key_ = EvalExpr(right_key_, right_t, right_->GetOutputSchema()).ToString();
    match_cursor_ = 0;
  }
}

void HashJoin::Close() {
  hash_table_.clear();
  right_buffer_.clear();
}
