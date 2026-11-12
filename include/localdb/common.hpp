#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <variant>
#include <optional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <functional>
#include <iomanip>
#include <cstring>

namespace localdb {

using u8  = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i64 = std::int64_t;

enum class ValueType : u8 {
    Null = 0,
    Int = 1,
    Real = 2,
    Text = 3,
    Blob = 4,
    Bool = 5
};

struct Value {
    ValueType type = ValueType::Null;
    std::variant<std::monostate, i64, double, std::string, std::vector<u8>, bool> data;

    static Value null() { return {}; }
    static Value integer(i64 v) { Value x; x.type = ValueType::Int; x.data = v; return x; }
    static Value real(double v) { Value x; x.type = ValueType::Real; x.data = v; return x; }
    static Value text(std::string v) { Value x; x.type = ValueType::Text; x.data = std::move(v); return x; }
    static Value blob(std::vector<u8> v) { Value x; x.type = ValueType::Blob; x.data = std::move(v); return x; }
    static Value boolean(bool v) { Value x; x.type = ValueType::Bool; x.data = v; return x; }

    bool isNull() const { return type == ValueType::Null; }

    std::string toString() const {
        switch (type) {
            case ValueType::Null: return "NULL";
            case ValueType::Int: return std::to_string(std::get<i64>(data));
            case ValueType::Real: {
                std::ostringstream oss; oss << std::setprecision(15) << std::get<double>(data); return oss.str();
            }
            case ValueType::Text: return std::get<std::string>(data);
            case ValueType::Blob: {
                const auto &b = std::get<std::vector<u8>>(data);
                std::ostringstream oss; oss << "0x";
                for (auto c : b) oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
                return oss.str();
            }
            case ValueType::Bool: return std::get<bool>(data) ? "true" : "false";
        }
        return "";
    }

    bool operator==(const Value &o) const {
        if (type != o.type) return false;
        return data == o.data;
    }
    bool operator!=(const Value &o) const { return !(*this == o); }
    bool operator<(const Value &o) const {
        if (type != o.type) return type < o.type;
        return data < o.data;
    }
};

struct Column {
    std::string name;
    ValueType type = ValueType::Text;
    bool primaryKey = false;
    bool notNull = false;
    bool unique = false;
    bool indexed = false;
    std::optional<Value> defaultValue;
};

struct Row {
    u64 id = 0;
    std::vector<Value> values;
    bool deleted = false;
};

enum class CmpOp { Eq, Ne, Lt, Le, Gt, Ge, Like, IsNull, NotNull };

struct Condition {
    std::string column;
    CmpOp op = CmpOp::Eq;
    Value value;
};

struct QueryResult {
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;
    u64 affected = 0;
    std::string message;
    bool ok = true;
};

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

inline std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

class DbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace localdb
