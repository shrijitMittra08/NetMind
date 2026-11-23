#pragma once

#include "netmind/common/Types.h"

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace netmind {

class ForwardingTable {
  public:
    enum class Source { DYNAMIC, STATIC };

    struct Entry {
        NodeId nextHop{};
        double cost{};
        Source source{Source::DYNAMIC};
    };

    void setNextHop(NodeId destination, NodeId nextHop) { install(destination, nextHop, 1.0); }
    [[nodiscard]] std::optional<NodeId> nextHop(NodeId destination) const;
    [[nodiscard]] bool contains(NodeId destination) const { return entries_.count(destination) > 0; }
    [[nodiscard]] std::optional<Entry> lookup(NodeId destination) const;
    void install(NodeId destination, NodeId nextHop, double cost);
    void clearDynamic();
    void addStatic(NodeId destination, NodeId nextHop, double cost);
    [[nodiscard]] std::vector<std::pair<NodeId, Entry>> toSortedVector() const;

  private:
    std::unordered_map<NodeId, Entry> entries_;
};

}
