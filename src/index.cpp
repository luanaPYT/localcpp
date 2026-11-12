#include "localdb/index.hpp"

namespace localdb {

void Index::add(const Value &key, u64 rowId) {
    map_.emplace(key, rowId);
}

void Index::remove(const Value &key, u64 rowId) {
    auto range = map_.equal_range(key);
    for (auto it = range.first; it != range.second; ) {
        if (it->second == rowId) it = map_.erase(it);
        else ++it;
    }
}

void Index::clear() { map_.clear(); }

std::vector<u64> Index::findEq(const Value &key) const {
    std::vector<u64> out;
    auto range = map_.equal_range(key);
    for (auto it = range.first; it != range.second; ++it) out.push_back(it->second);
    return out;
}

bool Index::hasValue(const Value &key) const {
    return map_.find(key) != map_.end();
}

} // namespace localdb
