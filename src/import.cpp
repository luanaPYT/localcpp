#include "localdb/database.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>

using namespace localdb;
namespace fs = std::filesystem;

static std::string timeToString(fs::file_time_type ft) {
    try {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t t = std::chrono::system_clock::to_time_t(sctp);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return buf;
    } catch (...) { return ""; }
}

static std::vector<u8> readFile(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open: " + p.string());
    in.seekg(0, std::ios::end);
    std::streamsize sz = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<u8> buf((size_t)sz);
    if (sz > 0) in.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage: localdb_import <db_path> <source_dir>\n";
        return 1;
    }
    fs::path dbPath = argv[1];
    fs::path src = argv[2];
    if (!fs::exists(src)) {
        std::cerr << "Source not found: " << src.string() << "\n";
        return 1;
    }
    try {
        Database db(dbPath);
        db.open();
        auto cr = db.execute("CREATE TABLE IF NOT EXISTS files (id INT PRIMARY KEY, rel_path TEXT, name TEXT, size INT, modified TEXT, is_dir BOOL, content BLOB)");
        if (!cr.ok) { std::cerr << cr.message << "\n"; return 1; }

        i64 nextId = 1;
        auto resMax = db.execute("SELECT id FROM files ORDER BY id DESC LIMIT 1");
        if (resMax.ok && !resMax.rows.empty() && !resMax.rows[0].empty() && resMax.rows[0][0].type == ValueType::Int) {
            nextId = std::get<i64>(resMax.rows[0][0].data) + 1;
        }

        std::vector<fs::path> items;
        if (fs::is_regular_file(src)) {
            items.push_back(src);
        } else {
            items.push_back(src);
            for (auto &e : fs::recursive_directory_iterator(src)) items.push_back(e.path());
        }
        std::sort(items.begin(), items.end());

        fs::path base = fs::is_directory(src) ? src : src.parent_path();
        u64 count = 0;
        for (const auto &p : items) {
            std::error_code ec;
            fs::path rel = fs::relative(p, base, ec);
            std::string rels = ec ? p.filename().string() : rel.string();
            if (rels == ".") rels = p.filename().string();
            bool isDir = fs::is_directory(p);
            i64 size = 0;
            std::vector<u8> content;
            if (!isDir && fs::is_regular_file(p)) {
                size = (i64)fs::file_size(p, ec);
                std::cout << "Importing: " << rels << " (" << size << " bytes)" << std::endl;
                content = readFile(p);
            } else {
                std::cout << "Importing: " << rels << " (dir)" << std::endl;
            }
            std::string mod;
            ec.clear();
            auto wt = fs::last_write_time(p, ec);
            if (!ec) mod = timeToString(wt);

            std::vector<Value> vals = {
                Value::integer(nextId++),
                Value::text(rels),
                Value::text(p.filename().string()),
                Value::integer(size),
                Value::text(mod),
                Value::boolean(isDir),
                Value::blob(std::move(content))
            };
            auto ir = db.insertRecord("files", vals);
            if (!ir.ok) { std::cerr << "Failed: " << rels << " -> " << ir.message << "\n"; return 1; }
            ++count;
        }
        db.close();
        std::cout << "Imported " << count << " item(s) from " << src.string() << " into " << fs::absolute(dbPath).string() << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
