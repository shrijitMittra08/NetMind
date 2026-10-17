#include "model/ForwardingTable.h"
#include <algorithm>
namespace netmind {
std::optional<ForwardingTable::Entry> ForwardingTable::lookup(NodeId dst) const {
    auto staticIt = static_.find(dst);
    if (staticIt != static_.end()) {
        return staticIt->second;
    }
    auto dynamicIt = dynamic_.find(dst);
    if (dynamicIt != dynamic_.end()) {
        return dynamicIt->second;
    }
    return std::nullopt;
}
void ForwardingTable::install(NodeId dst, NodeId nextHop, double cost) {
    if (static_.find(dst) != static_.end()) {
        return;
    }
    dynamic_[dst] = Entry{nextHop, cost, Source::DYNAMIC};
}
void ForwardingTable::replaceDynamic(NodeId dst, NodeId nextHop, double cost) {
    if (static_.find(dst) != static_.end()) {
        return;
    }
    dynamic_[dst] = Entry{nextHop, cost, Source::DYNAMIC};
}
void ForwardingTable::clearDynamic() {
    dynamic_.clear();
}
void ForwardingTable::addStatic(NodeId dst, NodeId nextHop, double cost) {
    static_[dst] = Entry{nextHop, cost, Source::STATIC};
}
void ForwardingTable::removeStatic(NodeId dst) {
    static_.erase(dst);
}
std::size_t ForwardingTable::size() const {
    return static_.size() + dynamic_.size();
}
std::vector<std::pair<NodeId, ForwardingTable::Entry>>
ForwardingTable::toSortedVector() const {
    std::vector<std::pair<NodeId, Entry>> result;
    result.reserve(size());
    for (const auto& entry : static_) {
        result.push_back(entry);
    }
    for (const auto& entry : dynamic_) {
        if (static_.find(entry.first) == static_.end()) {
            result.push_back(entry);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) {
                  return a.first < b.first;
              });
    return result;
}
}