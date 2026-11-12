#pragma once
#include "localdb/common.hpp"
#include "localdb/storage.hpp"
#include "localdb/index.hpp"

namespace localdb {

class Table {
public:
    Table(std::string name, std::vector<Column> columns, std::filesystem::path dir);

    const std::string &name() const { return name_; }
    const std::vector<Column> &columns() const { return columns_; }
    size_t columnIndex(const std::string &name) const;
    bool hasColumn(const std::string &name) const;

    void load();
    void persistAll();

    u64 insertRow(const std::vector<Value> &values);
    u64 insertRowNamed(const std::unordered_map<std::string, Value> &named);
    std::vector<Row> select(const std::vector<Condition> &conds) const;
    u64 update(const std::vector<Condition> &conds, const std::unordered_map<std::string, Value> &updates);
    u64 remove(const std::vector<Condition> &conds);
    u64 count() const;

    std::vector<Row> allRows() const;

private:
    bool match(const Row &row, const std::vector<Condition> &conds) const;
    void validateRowValues(std::vector<Value> &vals) const;
    void rebuildIndexes();
    void indexRow(const Row &row);
    void unindexRow(const Row &row);

    std::string name_;
    std::vector<Column> columns_;
    std::unordered_map<std::string, size_t> colIndex_;
    std::vector<Row> rows_;
    u64 nextId_ = 1;
    std::filesystem::path dir_;
    Storage storage_;
    std::unordered_map<std::string, Index> indexes_;
};

std::string serializeSchema(const std::string &name, const std::vector<Column> &cols);
std::pair<std::string, std::vector<Column>> deserializeSchema(const std::string &s);

} // namespace localdb
