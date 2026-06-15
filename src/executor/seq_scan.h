#pragma once
#include <vector>
#include "executor/executor.h"
#include "catalog/catalog.h"

class SeqScan : public Operator {
 public:
  SeqScan(TableInfo* table_info, std::string alias = "");

  void Init()           override;
  bool Next(Tuple* out) override;
  void Close()          override;
  const Schema& GetOutputSchema() const override { return output_schema_; }

 private:
  TableInfo*          table_info_;
  Schema              output_schema_;
  std::vector<Tuple>  buffer_;
  size_t              cursor_{0};
};
