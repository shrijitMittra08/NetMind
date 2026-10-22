#pragma once

#include "netmind/common/Types.h"

#include <optional>
#include <utility>
#include <vector>
#include <unordered_map>

namespace netmind {

class ForwardingTable {
public:
    enum class Source {
        DYNAMIC,
        STATIC
    };

    struct Entry {
        NodeId nextHop{};
        double cost{};
        Source source{Source::DYNAMIC};
    };

    void setNextHop(NodeId destination, NodeId nextHop);
    [[nodiscard]] std::optional<NodeId> nextHop(
        NodeId destination) const;
    [[nodiscard]] bool contains(NodeId destination) const;
    void clear();

    [[nodiscard]] std::optional<Entry> lookup(NodeId destination) const;
    void install(NodeId destination, NodeId nextHop, double cost);
    void clearDynamic();
    void addStatic(NodeId destination, NodeId nextHop, double cost);
    [[nodiscard]] std::vector<std::pair<NodeId, Entry>>
    toSortedVector() const;

private:
    std::unordered_map<NodeId, Entry> entries_;
};

}