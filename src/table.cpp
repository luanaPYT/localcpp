#include "localdb/table.hpp"
#include <algorithm>

namespace localdb {

static std::string typeToStr(ValueType t) {
    switch (t) {
        case ValueType::Null: return "NULL";
        case ValueType::Int: return "INT";
        case ValueType::Real: return "REAL";
        case ValueType::Text: return "TEXT";
        case ValueType::Blob: return "BLOB";
        case ValueType::Bool: return "BOOL";
    }
    return "TEXT";
}
static ValueType strToType(const std::string &s) {
    auto u = toLower(s);
    if (u == "int" || u == "integer") return ValueType::Int;
    if (u == "real" || u == "double" || u == "float") return ValueType::Real;
    if (u == "text" || u == "string") return ValueType::Text;
    if (u == "blob") return ValueType::Blob;
    if (u == "bool" || u == "boolean") return ValueType::Bool;
    return ValueType::Text;
}

Table::Table(std::string name, std::vector<Column> columns, std::filesystem::path dir)
    : name_(std::move(name)), columns_(std::move(columns)), dir_(std::move(dir)),
      storage_(dir_ / (name_ + ".tbl")) {
    for (size_t i = 0; i < columns_.size(); ++i) colIndex_[toLower(columns_[i].name)] = i;
}

size_t Table::columnIndex(const std::string &name) const {
    auto it = colIndex_.find(toLower(name));
    if (it == colIndex_.end()) throw DbError("Unknown column: " + name);
    return it->second;
}
bool Table::hasColumn(const std::string &name) const {
    return colIndex_.count(toLower(name)) > 0;
}

void Table::load() {
    storage_.open();
    rows_.clear();
    nextId_ = 1;
    auto lines = storage_.readAllLines();
    for (const auto &l : lines) {
        Row r = decodeRow(l);
        if (r.id >= nextId_) nextId_ = r.id + 1;
        rows_.push_back(std::move(r));
    }
    rebuildIndexes();
}

void Table::persistAll() {
    std::vector<std::string> lines;
    lines.reserve(rows_.size());
    for (const auto &r : rows_) lines.push_back(encodeRow(r));
    storage_.rewrite(lines);
}

void Table::rebuildIndexes() {
    indexes_.clear();
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].primaryKey || columns_[i].unique || columns_[i].indexed) {
            indexes_.emplace(toLower(columns_[i].name), Index{});
        }
    }
    for (const auto &r : rows_) if (!r.deleted) indexRow(r);
}

void Table::indexRow(const Row &row) {
    for (auto &kv : indexes_) {
        size_t idx = columnIndex(kv.first);
        if (idx < row.values.size()) kv.second.add(row.values[idx], row.id);
    }
}
void Table::unindexRow(const Row &row) {
    for (auto &kv : indexes_) {
        size_t idx = columnIndex(kv.first);
        if (idx < row.values.size()) kv.second.remove(row.values[idx], row.id);
    }
}

void Table::validateRowValues(std::vector<Value> &vals) const {
    if (vals.size() != columns_.size()) throw DbError("Column count mismatch");
    for (size_t i = 0; i < columns_.size(); ++i) {
        const auto &c = columns_[i];
        auto &v = vals[i];
        if (v.isNull() && c.notNull && !c.primaryKey) throw DbError("NOT NULL constraint failed: " + c.name);
        if (c.primaryKey && v.isNull()) throw DbError("PRIMARY KEY cannot be NULL: " + c.name);
        if (!v.isNull() && c.type != ValueType::Null && v.type != c.type) {
            // simple coercion
            try {
                if (c.type == ValueType::Int) {
                    if (v.type == ValueType::Real) v = Value::integer((i64)std::get<double>(v.data));
                    else if (v.type == ValueType::Text) v = Value::integer(std::stoll(std::get<std::string>(v.data)));
                    else if (v.type == ValueType::Bool) v = Value::integer(std::get<bool>(v.data)?1:0);
                } else if (c.type == ValueType::Real) {
                    if (v.type == ValueType::Int) v = Value::real((double)std::get<i64>(v.data));
                    else if (v.type == ValueType::Text) v = Value::real(std::stod(std::get<std::string>(v.data)));
                } else if (c.type == ValueType::Text) {
                    v = Value::text(v.toString());
                } else if (c.type == ValueType::Bool) {
                    if (v.type == ValueType::Int) v = Value::boolean(std::get<i64>(v.data)!=0);
                    else if (v.type == ValueType::Text) v = Value::boolean(toLower(std::get<std::string>(v.data))=="true"||std::get<std::string>(v.data)=="1");
                }
            } catch (...) { throw DbError("Type mismatch for column: " + c.name); }
        }
        if ((c.primaryKey || c.unique)) {
            auto it = indexes_.find(toLower(c.name));
            if (it != indexes_.end() && it->second.hasValue(v)) {
                throw DbError("UNIQUE constraint failed: " + c.name);
            }
        }
    }
}

u64 Table::insertRow(const std::vector<Value> &values) {
    std::vector<Value> vals = values;
    // fill defaults if short? expect exact
    if (vals.size() < columns_.size()) {
        for (size_t i = vals.size(); i < columns_.size(); ++i) {
            if (columns_[i].defaultValue) vals.push_back(*columns_[i].defaultValue);
            else vals.push_back(Value::null());
        }
    }
    validateRowValues(vals);
    Row r; r.id = nextId_++; r.values = std::move(vals); r.deleted = false;
    indexRow(r);
    rows_.push_back(r);
    storage_.appendLine(encodeRow(r));
    return r.id;
}

u64 Table::insertRowNamed(const std::unordered_map<std::string, Value> &named) {
    std::vector<Value> vals(columns_.size(), Value::null());
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].defaultValue) vals[i] = *columns_[i].defaultValue;
    }
    for (const auto &kv : named) {
        size_t idx = columnIndex(kv.first);
        vals[idx] = kv.second;
    }
    return insertRow(vals);
}

static bool likeMatch(const std::string &text, const std::string &pattern) {
    // simple % and _ wildcard
    size_t ti = 0, pi = 0, star = std::string::npos, match = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '_' || toLower(std::string(1,pattern[pi])) == toLower(std::string(1,text[ti])))) {
            ++ti; ++pi;
        } else if (pi < pattern.size() && pattern[pi] == '%') {
            star = pi++; match = ti;
        } else if (star != std::string::npos) {
            pi = star + 1; ti = ++match;
        } else return false;
    }
    while (pi < pattern.size() && pattern[pi] == '%') ++pi;
    return pi == pattern.size();
}

bool Table::match(const Row &row, const std::vector<Condition> &conds) const {
    if (row.deleted) return false;
    for (const auto &c : conds) {
        size_t idx = columnIndex(c.column);
        const Value &v = row.values[idx];
        bool ok = false;
        switch (c.op) {
            case CmpOp::Eq: ok = (v == c.value); break;
            case CmpOp::Ne: ok = (v != c.value); break;
            case CmpOp::Lt: ok = (v < c.value); break;
            case CmpOp::Le: ok = !(c.value < v); break;
            case CmpOp::Gt: ok = (c.value < v); break;
            case CmpOp::Ge: ok = !(v < c.value); break;
            case CmpOp::IsNull: ok = v.isNull(); break;
            case CmpOp::NotNull: ok = !v.isNull(); break;
            case CmpOp::Like: ok = (!v.isNull() && !c.value.isNull() && likeMatch(v.toString(), c.value.toString())); break;
        }
        if (!ok) return false;
    }
    return true;
}

std::vector<Row> Table::select(const std::vector<Condition> &conds) const {
    // try index for single equality
    if (conds.size() == 1 && conds[0].op == CmpOp::Eq) {
        auto it = indexes_.find(toLower(conds[0].column));
        if (it != indexes_.end()) {
            std::vector<Row> out;
            auto ids = it->second.findEq(conds[0].value);
            for (u64 id : ids) {
                for (const auto &r : rows_) if (r.id == id && !r.deleted) { out.push_back(r); break; }
            }
            return out;
        }
    }
    std::vector<Row> out;
    for (const auto &r : rows_) if (match(r, conds)) out.push_back(r);
    return out;
}

u64 Table::update(const std::vector<Condition> &conds, const std::unordered_map<std::string, Value> &updates) {
    u64 affected = 0;
    for (auto &r : rows_) {
        if (!match(r, conds)) continue;
        unindexRow(r);
        for (const auto &kv : updates) {
            size_t idx = columnIndex(kv.first);
            r.values[idx] = kv.second;
        }
        // basic validation for types/notnull (skip unique recheck complexity for brevity)
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].notNull && r.values[i].isNull()) throw DbError("NOT NULL constraint failed: " + columns_[i].name);
        }
        indexRow(r);
        affected++;
    }
    if (affected) persistAll();
    return affected;
}

u64 Table::remove(const std::vector<Condition> &conds) {
    u64 affected = 0;
    for (auto &r : rows_) {
        if (!match(r, conds)) continue;
        unindexRow(r);
        r.deleted = true;
        affected++;
    }
    if (affected) persistAll();
    return affected;
}

u64 Table::count() const {
    u64 c = 0; for (const auto &r : rows_) if (!r.deleted) ++c; return c;
}

std::vector<Row> Table::allRows() const {
    std::vector<Row> out;
    for (const auto &r : rows_) if (!r.deleted) out.push_back(r);
    return out;
}

std::string serializeSchema(const std::string &name, const std::vector<Column> &cols) {
    std::ostringstream oss;
    oss << name;
    for (const auto &c : cols) {
        oss << "|" << c.name << "," << typeToStr(c.type)
            << "," << (c.primaryKey?"1":"0")
            << "," << (c.notNull?"1":"0")
            << "," << (c.unique?"1":"0")
            << "," << (c.indexed?"1":"0");
    }
    return oss.str();
}

std::pair<std::string, std::vector<Column>> deserializeSchema(const std::string &s) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= s.size()) {
        size_t p = s.find('|', start);
        if (p == std::string::npos) { parts.push_back(s.substr(start)); break; }
        parts.push_back(s.substr(start, p-start)); start = p+1;
    }
    if (parts.empty()) return {"",{}};
    std::string name = parts[0];
    std::vector<Column> cols;
    for (size_t i = 1; i < parts.size(); ++i) {
        std::vector<std::string> f;
        size_t st = 0;
        while (st <= parts[i].size()) {
            size_t p = parts[i].find(',', st);
            if (p == std::string::npos) { f.push_back(parts[i].substr(st)); break; }
            f.push_back(parts[i].substr(st, p-st)); st = p+1;
        }
        if (f.size() >= 6) {
            Column c; c.name = f[0]; c.type = strToType(f[1]);
            c.primaryKey = f[2]=="1"; c.notNull = f[3]=="1"; c.unique = f[4]=="1"; c.indexed = f[5]=="1";
            cols.push_back(std::move(c));
        }
    }
    return {name, cols};
}

} // namespace localdb
