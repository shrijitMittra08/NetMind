#include "netmind/model/ForwardingTable.h"

#include "netmind/common/Errors.h"

#include <algorithm>
#include <cmath>

namespace netmind {

void ForwardingTable::setNextHop(NodeId destination, NodeId nextHopId) {
    install(destination, nextHopId, 1.0);
}

std::optional<NodeId> ForwardingTable::nextHop(NodeId destination) const {
    const auto entry = lookup(destination);
    if (!entry) {
        return std::nullopt;
    }

    return entry->nextHop;
}

bool ForwardingTable::contains(NodeId destination) const {
    return entries_.find(destination) != entries_.end();
}

void ForwardingTable::clear() {
    entries_.clear();
}

std::optional<ForwardingTable::Entry> ForwardingTable::lookup(
    NodeId destination) const {
    const auto it = entries_.find(destination);
    if (it == entries_.end()) {
        return std::nullopt;
    }

    return it->second;
}

void ForwardingTable::install(
    NodeId destination,
    NodeId nextHopId,
    double cost) {
    if (!std::isfinite(cost) || cost < 0.0) {
        throw ConfigError("Route cost must be finite and non-negative");
    }

    const auto it = entries_.find(destination);
    if (it != entries_.end() && it->second.source == Source::STATIC) {
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

void ForwardingTable::addStatic(
    NodeId destination,
    NodeId nextHopId,
    double cost) {
    if (!std::isfinite(cost) || cost < 0.0) {
        throw ConfigError("Route cost must be finite and non-negative");
    }

    entries_[destination] = Entry{nextHopId, cost, Source::STATIC};
}

std::vector<std::pair<NodeId, ForwardingTable::Entry>>
ForwardingTable::toSortedVector() const {
    std::vector<std::pair<NodeId, Entry>> routes;
    routes.reserve(entries_.size());

    for (const auto& item : entries_) {
        routes.push_back(item);
    }

    std::sort(
        routes.begin(),
        routes.end(),
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        });

    return routes;
}

}