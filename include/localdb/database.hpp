#pragma once
#include "localdb/table.hpp"

namespace localdb {

class Database {
public:
    explicit Database(std::filesystem::path root);

    void open();
    void close();

    QueryResult execute(const std::string &sql);

    QueryResult createTable(const std::string &name, const std::vector<Column> &cols, bool ifNotExists = false);
    QueryResult dropTable(const std::string &name, bool ifExists = false);
    QueryResult listTables() const;
    QueryResult describeTable(const std::string &name) const;
    QueryResult insertRecord(const std::string &name, const std::vector<Value> &values);

    Table &table(const std::string &name);
    const Table &table(const std::string &name) const;
    bool hasTable(const std::string &name) const;

    const std::filesystem::path &root() const { return root_; }

private:
    void loadCatalog();
    void saveCatalog();
    std::filesystem::path tableDir(const std::string &name) const;

    std::filesystem::path root_;
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
    mutable std::shared_mutex mutex_;
    bool open_ = false;
};

} // namespace localdb
