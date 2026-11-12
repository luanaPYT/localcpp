#pragma once
#include "localdb/common.hpp"

namespace localdb {

class Index {
public:
    void add(const Value &key, u64 rowId);
    void remove(const Value &key, u64 rowId);
    void clear();
    std::vector<u64> findEq(const Value &key) const;
    bool hasValue(const Value &key) const;
    size_t size() const { return map_.size(); }
private:
    std::multimap<Value, u64> map_;
};

} // namespace localdb
