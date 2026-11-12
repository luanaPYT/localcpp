#pragma once
#include "localdb/common.hpp"

namespace localdb {

struct CreateTableStmt {
    std::string name;
    std::vector<Column> columns;
    bool ifNotExists = false;
};

struct DropTableStmt {
    std::string name;
    bool ifExists = false;
};

struct InsertStmt {
    std::string table;
    std::vector<std::string> columns; // optional
    std::vector<Value> values;
};

struct SelectStmt {
    std::string table;
    std::vector<std::string> columns; // empty or * => all
    std::vector<Condition> where;
    std::optional<std::string> orderBy;
    bool desc = false;
    std::optional<u64> limit;
};

struct UpdateStmt {
    std::string table;
    std::unordered_map<std::string, Value> sets;
    std::vector<Condition> where;
};

struct DeleteStmt {
    std::string table;
    std::vector<Condition> where;
};

struct ParsedStatement {
    enum class Kind { Create, Drop, Insert, Select, Update, Delete, ShowTables, Describe, Help, Exit, Unknown } kind = Kind::Unknown;
    CreateTableStmt create;
    DropTableStmt drop;
    InsertStmt insert;
    SelectStmt select;
    UpdateStmt update;
    DeleteStmt del;
    std::string describeName;
    std::string raw;
};

class Parser {
public:
    ParsedStatement parse(const std::string &sql) const;
};

} // namespace localdb
