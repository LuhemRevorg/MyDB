#pragma once
#include <string>
#include <vector>
#include "catalog/catalog.h"
#include "parser/ast.h"
#include "planner/planner.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

struct QueryResult {
  std::vector<std::string>              columns;
  std::vector<std::vector<std::string>> rows;
  std::string                           message;
};

class Database {
 public:
  Database(const std::string& db_path, size_t pool_size = 256);
  QueryResult Execute(const std::string& sql);

 private:
  QueryResult ExecCreate(const CreateTableStmt& stmt);
  QueryResult ExecInsert(const InsertStmt& stmt);
  QueryResult ExecSelect(const SelectStmt& stmt);
  QueryResult ExecExplain(const ExplainStmt& stmt);

  DiskManager       disk_manager_;
  BufferPoolManager bpm_;
  Catalog           catalog_;
  Planner           planner_;
};
