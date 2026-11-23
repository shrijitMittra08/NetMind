#include "netmind/model/ForwardingTable.h"

#include "netmind/common/Errors.h"

#include <algorithm>
#include <cmath>

namespace netmind {
namespace {

void requireValidCost(double cost) {
    if (!std::isfinite(cost) || cost < 0.0) {
        throw ConfigError("Route cost must be finite and non-negative");
    }
}

}

std::optional<NodeId> ForwardingTable::nextHop(NodeId destination) const {
    if (!contains(destination)) {
        return std::nullopt;
    }
    return entries_.at(destination).nextHop;
}

std::optional<ForwardingTable::Entry> ForwardingTable::lookup(NodeId destination) const {
    if (!contains(destination)) {
        return std::nullopt;
    }
    return entries_.at(destination);
}

void ForwardingTable::install(NodeId destination, NodeId nextHopId, double cost) {
    requireValidCost(cost);
    if (contains(destination) && entries_.at(destination).source == Source::STATIC) {
        return;
    }
    entries_[destination] = Entry{nextHopId, cost, Source::DYNAMIC};
}

void ForwardingTable::clearDynamic() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.source == Source::DYNAMIC) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void ForwardingTable::addStatic(NodeId destination, NodeId nextHopId, double cost) {
    requireValidCost(cost);
    entries_[destination] = Entry{nextHopId, cost, Source::STATIC};
}

std::vector<std::pair<NodeId, ForwardingTable::Entry>> ForwardingTable::toSortedVector() const {
    using Route = std::pair<NodeId, Entry>;
    std::vector<Route> routes(entries_.begin(), entries_.end());
    std::sort(routes.begin(), routes.end(), [](const Route& left, const Route& right) {
        return left.first < right.first;
    });
    return routes;
}

}
