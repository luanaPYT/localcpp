#include "localdb/storage.hpp"

namespace localdb {

Storage::Storage(const std::filesystem::path &path) : path_(path) {}

Storage::~Storage() { close(); }

void Storage::open() {
    std::filesystem::create_directories(path_.parent_path());
    if (!std::filesystem::exists(path_)) {
        std::ofstream create(path_, std::ios::binary);
    }
    stream_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!stream_.is_open()) {
        throw DbError("Cannot open storage: " + path_.string());
    }
    open_ = true;
}

void Storage::close() {
    if (open_) {
        stream_.flush();
        stream_.close();
        open_ = false;
    }
}

bool Storage::isOpen() const { return open_; }

void Storage::appendLine(const std::string &line) {
    if (!open_) open();
    stream_.clear();
    stream_.seekp(0, std::ios::end);
    stream_ << line << "\n";
}

std::vector<std::string> Storage::readAllLines() {
    if (!open_) open();
    std::vector<std::string> lines;
    stream_.clear();
    stream_.seekg(0, std::ios::beg);
    std::string line;
    while (std::getline(stream_, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

void Storage::rewrite(const std::vector<std::string> &lines) {
    close();
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    for (const auto &l : lines) out << l << "\n";
    out.close();
    open();
}

void Storage::flush() {
    if (open_) stream_.flush();
}

static std::string hexEncode(const std::vector<u8> &b) {
    static const char *hex = "0123456789abcdef";
    std::string out; out.resize(b.size() * 2);
    for (size_t i = 0; i < b.size(); ++i) {
        out[i*2] = hex[(b[i] >> 4) & 0xF];
        out[i*2+1] = hex[b[i] & 0xF];
    }
    return out;
}

static std::vector<u8> hexDecode(const std::string &s) {
    auto val = [](char c)->int{
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return 0;
    };
    std::vector<u8> out(s.size()/2);
    for (size_t i = 0; i < out.size(); ++i) out[i] = (u8)((val(s[i*2])<<4) | val(s[i*2+1]));
    return out;
}

static std::string escapeText(const std::string &s) {
    std::string o; o.reserve(s.size()+4);
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '|':  o += "\\p"; break;
            default: o += c; break;
        }
    }
    return o;
}

static std::string unescapeText(const std::string &s) {
    std::string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            switch (n) {
                case '\\': o += '\\'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'p': o += '|'; break;
                default: o += n; break;
            }
        } else o += s[i];
    }
    return o;
}

std::string encodeValue(const Value &v) {
    std::ostringstream oss;
    oss << (int)v.type << ":";
    switch (v.type) {
        case ValueType::Null: break;
        case ValueType::Int: oss << std::get<i64>(v.data); break;
        case ValueType::Real: oss << std::setprecision(17) << std::get<double>(v.data); break;
        case ValueType::Text: oss << escapeText(std::get<std::string>(v.data)); break;
        case ValueType::Blob: oss << hexEncode(std::get<std::vector<u8>>(v.data)); break;
        case ValueType::Bool: oss << (std::get<bool>(v.data) ? '1' : '0'); break;
    }
    return oss.str();
}

Value decodeValue(const std::string &s) {
    auto pos = s.find(':');
    if (pos == std::string::npos) return Value::null();
    int t = std::stoi(s.substr(0, pos));
    std::string payload = s.substr(pos + 1);
    switch ((ValueType)t) {
        case ValueType::Null: return Value::null();
        case ValueType::Int: return Value::integer(payload.empty() ? 0 : std::stoll(payload));
        case ValueType::Real: return Value::real(payload.empty() ? 0.0 : std::stod(payload));
        case ValueType::Text: return Value::text(unescapeText(payload));
        case ValueType::Blob: return Value::blob(hexDecode(payload));
        case ValueType::Bool: return Value::boolean(payload == "1");
    }
    return Value::null();
}

std::string encodeRow(const Row &r) {
    std::ostringstream oss;
    oss << r.id << "|" << (r.deleted ? 1 : 0) << "|" << r.values.size();
    for (const auto &v : r.values) {
        oss << "|" << encodeValue(v);
    }
    return oss.str();
}

Row decodeRow(const std::string &s) {
    Row r;
    size_t start = 0;
    auto next = [&](std::string &out)->bool{
        if (start > s.size()) return false;
        size_t p = s.find('|', start);
        if (p == std::string::npos) { out = s.substr(start); start = s.size()+1; return true; }
        out = s.substr(start, p - start);
        start = p + 1;
        return true;
    };
    std::string tok;
    if (!next(tok)) return r; r.id = tok.empty() ? 0 : std::stoull(tok);
    if (!next(tok)) return r; r.deleted = (tok == "1");
    if (!next(tok)) return r; size_t n = tok.empty() ? 0 : (size_t)std::stoull(tok);
    r.values.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        // remaining is encoded values separated by | but encodeValue has no | except payload escaped? we escaped '|'
        if (!next(tok)) break;
        r.values.push_back(decodeValue(tok));
    }
    return r;
}

} // namespace localdb
