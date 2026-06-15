#pragma once
#include <unordered_map>
#include <vector>
#include "executor/executor.h"

class HashJoin : public Operator {
 public:
  // left  = build side (smaller table), right = probe side.
  // left_key / right_key are the join key expressions for each side.
  HashJoin(std::unique_ptr<Operator> left,
           std::unique_ptr<Operator> right,
           const Expr* left_key,
           const Expr* right_key);

  void Init()           override;
  bool Next(Tuple* out) override;
  void Close()          override;
  const Schema& GetOutputSchema() const override { return output_schema_; }

 private:
  static Tuple Concat(const Tuple& l, const Tuple& r,
                      const Schema& ls, const Schema& rs);

  std::unique_ptr<Operator> left_;
  std::unique_ptr<Operator> right_;
  const Expr*               left_key_;
  const Expr*               right_key_;
  Schema                    output_schema_;

  // hash table: string(key) → list of left tuples with that key
  std::unordered_map<std::string, std::vector<Tuple>> hash_table_;

  // Probe state
  std::vector<Tuple> right_buffer_;
  size_t             right_cursor_{0};
  size_t             match_cursor_{0};  // index into current hash bucket
  std::string        current_key_;
};
