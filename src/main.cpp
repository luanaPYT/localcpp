#include "localdb/database.hpp"
#include <iostream>
#include <iomanip>

using namespace localdb;

static void printResult(const QueryResult &r) {
    if (!r.ok) {
        std::cout << r.message << "\n";
        return;
    }
    if (!r.columns.empty()) {
        std::vector<size_t> widths(r.columns.size(), 4);
        for (size_t i = 0; i < r.columns.size(); ++i) widths[i] = std::max(widths[i], r.columns[i].size());
        for (const auto &row : r.rows) {
            for (size_t i = 0; i < row.size() && i < widths.size(); ++i) {
                widths[i] = std::max(widths[i], row[i].toString().size());
            }
        }
        auto sep = [&]() {
            std::cout << "+";
            for (auto w : widths) std::cout << std::string(w + 2, '-') << "+";
            std::cout << "\n";
        };
        sep();
        std::cout << "|";
        for (size_t i = 0; i < r.columns.size(); ++i) std::cout << " " << std::left << std::setw((int)widths[i]) << r.columns[i] << " |";
        std::cout << "\n";
        sep();
        for (const auto &row : r.rows) {
            std::cout << "|";
            for (size_t i = 0; i < widths.size(); ++i) {
                std::string v = i < row.size() ? row[i].toString() : "NULL";
                std::cout << " " << std::left << std::setw((int)widths[i]) << v << " |";
            }
            std::cout << "\n";
        }
        sep();
    }
    if (!r.message.empty()) std::cout << r.message << "\n";
}

int main(int argc, char **argv) {
    std::filesystem::path dbPath = "data";
    if (argc > 1) dbPath = argv[1];
    try {
        Database db(dbPath);
        db.open();
        std::cout << "LocalDB - local storage database\n";
        std::cout << "Data path: " << std::filesystem::absolute(dbPath).string() << "\n";
        std::cout << "Type 'help' for commands, 'exit' to quit.\n\n";

        std::string line;
        while (true) {
            std::cout << "db> " << std::flush;
            if (!std::getline(std::cin, line)) break;
            std::string sql = trim(line);
            if (sql.empty()) continue;
            // allow multi-line until semicolon? simple: execute per line
            if (!sql.empty() && sql.back() == ';') sql.pop_back();
            sql = trim(sql);
            if (toLower(sql) == "exit" || toLower(sql) == "quit") break;
            auto res = db.execute(sql);
            printResult(res);
        }
        db.close();
    } catch (const std::exception &e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
