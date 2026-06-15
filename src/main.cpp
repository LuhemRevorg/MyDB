#include <iostream>
#include <string>
#include "database.h"

static void PrintResult(const QueryResult& result) {
  if (!result.message.empty()) { std::cout << result.message << "\n"; return; }

  if (result.rows.empty()) { std::cout << "(0 rows)\n"; return; }

  // Compute column widths
  std::vector<size_t> widths(result.columns.size());
  for (size_t i = 0; i < result.columns.size(); ++i)
    widths[i] = result.columns[i].size();
  for (const auto& row : result.rows)
    for (size_t i = 0; i < row.size(); ++i)
      widths[i] = std::max(widths[i], row[i].size());

  auto separator = [&]() {
    std::cout << "+";
    for (size_t w : widths) std::cout << std::string(w + 2, '-') << "+";
    std::cout << "\n";
  };

  separator();
  std::cout << "|";
  for (size_t i = 0; i < result.columns.size(); ++i)
    std::cout << " " << result.columns[i] << std::string(widths[i] - result.columns[i].size(), ' ') << " |";
  std::cout << "\n";
  separator();
  for (const auto& row : result.rows) {
    std::cout << "|";
    for (size_t i = 0; i < row.size(); ++i)
      std::cout << " " << row[i] << std::string(widths[i] - row[i].size(), ' ') << " |";
    std::cout << "\n";
  }
  separator();
  std::cout << result.rows.size() << " row(s)\n";
}

int main(int argc, char* argv[]) {
  std::string db_path = argc > 1 ? argv[1] : "mydb.db";
  Database db{db_path};
  std::cout << "mydb — type SQL or .quit to exit\n";

  std::string line, accumulated;
  while (true) {
    std::cout << (accumulated.empty() ? "mydb> " : "   ...> ") << std::flush;
    if (!std::getline(std::cin, line)) break;

    if (line == ".quit" || line == ".exit") break;
    if (line.empty()) continue;

    accumulated += (accumulated.empty() ? "" : " ") + line;

    // Execute when the input ends with ';'
    if (!accumulated.empty() && accumulated.back() == ';') {
      try {
        PrintResult(db.Execute(accumulated));
      } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
      }
      accumulated.clear();
    }
  }
  return 0;
}
