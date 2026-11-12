#include "localdb/database.hpp"
#include "localdb/query.hpp"

namespace localdb {

Database::Database(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path Database::tableDir(const std::string &name) const {
    return root_ / "tables" / toLower(name);
}

void Database::open() {
    std::unique_lock lk(mutex_);
    std::filesystem::create_directories(root_ / "tables");
    loadCatalog();
    open_ = true;
}

void Database::close() {
    std::unique_lock lk(mutex_);
    if (open_) {
        saveCatalog();
        for (auto &kv : tables_) kv.second->persistAll();
        tables_.clear();
        open_ = false;
    }
}

void Database::loadCatalog() {
    std::filesystem::path cat = root_ / "catalog.meta";
    if (!std::filesystem::exists(cat)) return;
    std::ifstream in(cat, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.empty()) continue;
        auto sc = deserializeSchema(line);
        if (sc.first.empty()) continue;
        auto dir = tableDir(sc.first);
        auto t = std::make_unique<Table>(sc.first, sc.second, dir);
        try { t->load(); } catch(...) {}
        tables_[toLower(sc.first)] = std::move(t);
    }
}

void Database::saveCatalog() {
    std::filesystem::path cat = root_ / "catalog.meta";
    std::ofstream out(cat, std::ios::binary | std::ios::trunc);
    for (const auto &kv : tables_) {
        out << serializeSchema(kv.second->name(), kv.second->columns()) << "\n";
    }
}

bool Database::hasTable(const std::string &name) const {
    return tables_.count(toLower(name)) > 0;
}

Table &Database::table(const std::string &name) {
    auto it = tables_.find(toLower(name));
    if (it == tables_.end()) throw DbError("Table not found: " + name);
    return *it->second;
}
const Table &Database::table(const std::string &name) const {
    auto it = tables_.find(toLower(name));
    if (it == tables_.end()) throw DbError("Table not found: " + name);
    return *it->second;
}

QueryResult Database::createTable(const std::string &name, const std::vector<Column> &cols, bool ifNotExists) {
    std::unique_lock lk(mutex_);
    if (hasTable(name)) {
        if (ifNotExists) return { {}, {}, 0, "Table already exists (skipped)", true };
        throw DbError("Table already exists: " + name);
    }
    if (cols.empty()) throw DbError("Table must have at least one column");
    auto dir = tableDir(name);
    std::filesystem::create_directories(dir);
    auto t = std::make_unique<Table>(name, cols, dir);
    t->load();
    tables_[toLower(name)] = std::move(t);
    saveCatalog();
    return { {}, {}, 0, "Table created: " + name, true };
}

QueryResult Database::dropTable(const std::string &name, bool ifExists) {
    std::unique_lock lk(mutex_);
    auto it = tables_.find(toLower(name));
    if (it == tables_.end()) {
        if (ifExists) return { {}, {}, 0, "Table does not exist (skipped)", true };
        throw DbError("Table not found: " + name);
    }
    tables_.erase(it);
    std::error_code ec;
    std::filesystem::remove_all(tableDir(name), ec);
    saveCatalog();
    return { {}, {}, 0, "Table dropped: " + name, true };
}

QueryResult Database::listTables() const {
    std::shared_lock lk(mutex_);
    QueryResult r; r.columns = {"table_name", "rows"};
    for (const auto &kv : tables_) {
        r.rows.push_back({Value::text(kv.second->name()), Value::integer((i64)kv.second->count())});
    }
    std::sort(r.rows.begin(), r.rows.end(), [](auto&a,auto&b){return a[0].toString() < b[0].toString();});
    r.message = std::to_string(r.rows.size()) + " table(s)";
    return r;
}

QueryResult Database::describeTable(const std::string &name) const {
    std::shared_lock lk(mutex_);
    const auto &t = table(name);
    QueryResult r; r.columns = {"name","type","pk","not_null","unique","indexed"};
    for (const auto &c : t.columns()) {
        auto ts = [](ValueType vt){ switch(vt){case ValueType::Int:return "INT";case ValueType::Real:return "REAL";case ValueType::Text:return "TEXT";case ValueType::Blob:return "BLOB";case ValueType::Bool:return "BOOL";default:return "NULL";} };
        r.rows.push_back({Value::text(c.name), Value::text(ts(c.type)), Value::boolean(c.primaryKey), Value::boolean(c.notNull), Value::boolean(c.unique), Value::boolean(c.indexed)});
    }
    r.message = "ok";
    return r;
}

QueryResult Database::insertRecord(const std::string &name, const std::vector<Value> &values) {
    std::unique_lock lk(mutex_);
    auto &t = table(name);
    u64 id = t.insertRow(values);
    QueryResult r; r.affected = 1; r.message = "Inserted row id=" + std::to_string(id); return r;
}

QueryResult Database::execute(const std::string &sql) {
    Parser p;
    auto st = p.parse(sql);
    try {
        switch (st.kind) {
            case ParsedStatement::Kind::Create:
                return createTable(st.create.name, st.create.columns, st.create.ifNotExists);
            case ParsedStatement::Kind::Drop:
                return dropTable(st.drop.name, st.drop.ifExists);
            case ParsedStatement::Kind::ShowTables:
                return listTables();
            case ParsedStatement::Kind::Describe:
                return describeTable(st.describeName);
            case ParsedStatement::Kind::Insert: {
                std::unique_lock lk(mutex_);
                auto &t = table(st.insert.table);
                u64 id = 0;
                if (!st.insert.columns.empty()) {
                    std::unordered_map<std::string, Value> named;
                    for (size_t i=0;i<st.insert.columns.size() && i<st.insert.values.size();++i) named[st.insert.columns[i]] = st.insert.values[i];
                    id = t.insertRowNamed(named);
                } else {
                    id = t.insertRow(st.insert.values);
                }
                QueryResult r; r.affected=1; r.message="Inserted row id=" + std::to_string(id); return r;
            }
            case ParsedStatement::Kind::Select: {
                std::shared_lock lk(mutex_);
                auto &t = table(st.select.table);
                auto rows = t.select(st.select.where);
                // order/limit basic
                if (st.select.orderBy && t.hasColumn(*st.select.orderBy)) {
                    size_t oi = t.columnIndex(*st.select.orderBy);
                    std::sort(rows.begin(), rows.end(), [&](const Row&a,const Row&b){
                        if (oi>=a.values.size()||oi>=b.values.size()) return false;
                        return st.select.desc ? (b.values[oi] < a.values[oi]) : (a.values[oi] < b.values[oi]);
                    });
                }
                if (st.select.limit && rows.size() > *st.select.limit) rows.resize(*st.select.limit);
                QueryResult r;
                std::vector<size_t> colIdx;
                if (st.select.columns.empty() || (st.select.columns.size()==1 && st.select.columns[0]=="*")) {
                    for (const auto &c : t.columns()) { r.columns.push_back(c.name); colIdx.push_back(t.columnIndex(c.name)); }
                } else {
                    for (const auto &cn : st.select.columns) { r.columns.push_back(cn); colIdx.push_back(t.columnIndex(cn)); }
                }
                for (const auto &row : rows) {
                    std::vector<Value> out;
                    for (auto ci : colIdx) out.push_back(ci < row.values.size() ? row.values[ci] : Value::null());
                    r.rows.push_back(std::move(out));
                }
                r.message = std::to_string(r.rows.size()) + " row(s)";
                return r;
            }
            case ParsedStatement::Kind::Update: {
                std::unique_lock lk(mutex_);
                auto &t = table(st.update.table);
                std::unordered_map<std::string, Value> sets;
                for (auto &kv : st.update.sets) sets[kv.first]=kv.second; // keys lowercased in parser? mixed
                // normalize keys to actual column names
                std::unordered_map<std::string, Value> norm;
                for (auto &kv : sets) { norm[t.columns()[t.columnIndex(kv.first)].name] = kv.second; }
                u64 n = t.update(st.update.where, norm);
                QueryResult r; r.affected=n; r.message="Updated " + std::to_string(n) + " row(s)"; return r;
            }
            case ParsedStatement::Kind::Delete: {
                std::unique_lock lk(mutex_);
                auto &t = table(st.del.table);
                u64 n = t.remove(st.del.where);
                QueryResult r; r.affected=n; r.message="Deleted " + std::to_string(n) + " row(s)"; return r;
            }
            case ParsedStatement::Kind::Help: {
                QueryResult r; r.columns={"topic","info"};
                r.rows={{Value::text("SQL"), Value::text("CREATE TABLE, DROP TABLE, INSERT, SELECT, UPDATE, DELETE, SHOW TABLES, DESCRIBE")},
                        {Value::text("Types"), Value::text("INT, REAL, TEXT, BLOB, BOOL")},
                        {Value::text("Meta"), Value::text("help, exit")}};
                r.message="ok"; return r;
            }
            case ParsedStatement::Kind::Exit:
                return { {}, {}, 0, "bye", true };
            default:
                return { {}, {}, 0, "Unknown or unsupported statement", false };
        }
    } catch (const std::exception &e) {
        return { {}, {}, 0, std::string("Error: ") + e.what(), false };
    }
}

} // namespace localdb
