#pragma once
#include "localdb/common.hpp"

namespace localdb {

class Storage {
public:
    explicit Storage(const std::filesystem::path &path);
    ~Storage();

    void open();
    void close();
    bool isOpen() const;

    void appendLine(const std::string &line);
    std::vector<std::string> readAllLines();
    void rewrite(const std::vector<std::string> &lines);
    void flush();

    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
    std::fstream stream_;
    bool open_ = false;
};

std::string encodeValue(const Value &v);
Value decodeValue(const std::string &s);

std::string encodeRow(const Row &r);
Row decodeRow(const std::string &s);

} // namespace localdb
