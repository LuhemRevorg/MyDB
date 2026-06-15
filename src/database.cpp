#include "database.h"
#include <sstream>
#include <stdexcept>
#include "parser/lexer.h"
#include "parser/parser.h"

Database::Database(const std::string& db_path, size_t pool_size)
    : disk_manager_{db_path},
      bpm_{pool_size, &disk_manager_},
      catalog_{&bpm_},
      planner_{&catalog_} {}

QueryResult Database::Execute(const std::string& sql) {
  Lexer  lexer{sql};
  Parser parser{lexer.Tokenize()};
  Statement stmt = parser.Parse();
  return std::visit([this](auto& s) -> QueryResult {
    using T = std::decay_t<decltype(s)>;
    if constexpr (std::is_same_v<T, CreateTableStmt>) return ExecCreate(s);
    if constexpr (std::is_same_v<T, InsertStmt>)      return ExecInsert(s);
    if constexpr (std::is_same_v<T, SelectStmt>)      return ExecSelect(s);
    if constexpr (std::is_same_v<T, ExplainStmt>)     return ExecExplain(s);
    return {};
  }, stmt);
}

QueryResult Database::ExecCreate(const CreateTableStmt& stmt) {
  std::vector<Column> cols;
  for (const auto& def : stmt.columns)
    cols.push_back({def.name, {def.type_id, def.max_len}});
  catalog_.CreateTable(stmt.table_name, Schema{cols});
  return {{}, {}, "Table '" + stmt.table_name + "' created."};
}

QueryResult Database::ExecInsert(const InsertStmt& stmt) {
  TableInfo* info = catalog_.GetTable(stmt.table_name);
  const Schema& schema = info->schema;
  if (stmt.values.size() != schema.GetColumnCount())
    throw std::runtime_error("Column count mismatch in INSERT");
  std::vector<Value> vals;
  for (size_t i = 0; i < stmt.values.size(); ++i) {
    const Expr* e = stmt.values[i].get();
    const ColumnType& ct = schema.GetColumn(i).type;
    if (ct.type_id == TypeId::INT) {
      if (e->type != ExprType::INT_LITERAL)
        throw std::runtime_error("Expected integer for column " + schema.GetColumn(i).name);
      vals.push_back(Value::MakeInt(e->int_val));
    } else {
      if (e->type != ExprType::STRING_LITERAL)
        throw std::runtime_error("Expected string for column " + schema.GetColumn(i).name);
      vals.push_back(Value::MakeVarchar(e->str_val, ct.max_len));
    }
  }
  Tuple t{vals, schema};
  RID rid;
  if (!info->heap_file->InsertTuple(t, &rid))
    throw std::runtime_error("INSERT failed — storage full");
  return {{}, {}, "Inserted 1 row."};
}

QueryResult Database::ExecSelect(const SelectStmt& stmt) {
  auto op = planner_.Plan(stmt);
  const Schema& out_schema = op->GetOutputSchema();

  QueryResult result;
  for (size_t i = 0; i < out_schema.GetColumnCount(); ++i)
    result.columns.push_back(out_schema.GetColumn(i).name);

  op->Init();
  Tuple t;
  while (op->Next(&t)) {
    std::vector<std::string> row;
    for (size_t i = 0; i < out_schema.GetColumnCount(); ++i)
      row.push_back(t.GetValue(out_schema, i).ToString());
    result.rows.push_back(std::move(row));
  }
  op->Close();
  return result;
}

QueryResult Database::ExecExplain(const ExplainStmt& stmt) {
  std::string plan_text = planner_.Explain(*stmt.select);
  QueryResult result;
  result.columns = {"plan"};
  std::istringstream ss(plan_text);
  std::string line;
  while (std::getline(ss, line))
    result.rows.push_back({line});
  return result;
}
