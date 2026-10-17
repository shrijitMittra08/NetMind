#pragma once
#include <unordered_map>
#include <optional>
#include <utility>
#include <vector>

#include "common/Types.h"

namespace netmind {

class ForwardingTable {
public:
    enum class Source {
        DYNAMIC,
        STATIC
    };

    struct Entry {
        NodeId nextHop;
        double cost;
        Source source;
    };

    std::optional<Entry> lookup(NodeId dst) const;

    void install(NodeId dst, NodeId nextHop, double cost);

    void replaceDynamic(NodeId dst, NodeId nextHop, double cost);

    void clearDynamic();

    void addStatic(NodeId dst, NodeId nextHop, double cost);

    void removeStatic(NodeId dst);

    std::size_t size() const;

    std::vector<std::pair<NodeId, Entry>> toSortedVector() const;

private:
    std::unordered_map<NodeId, Entry> dynamic_;
    std::unordered_map<NodeId, Entry> static_;
};

}  // namespace netmind