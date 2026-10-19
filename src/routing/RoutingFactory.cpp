#include "netmind/routing/RoutingFactory.h"

#include <memory>

#include "netmind/common/Errors.h"
#include "netmind/routing/BFSRouting.h"
#include "netmind/routing/BellmanFordRouting.h"
#include "netmind/routing/DFSRouting.h"
#include "netmind/routing/DjikstraRouting.h"

namespace netmind {

std::unique_ptr<RoutingAlgorithm> RoutingFactory::makeAlgorithm(const std::string& name) {
    if (name == "bfs") {
        return std::make_unique<BFSRouting>();
    }

    if (name == "dfs") {
        return std::make_unique<DFSRouting>();
    }

    if (name == "dijkstra") {
        return std::make_unique<DijkstraRouting>();
    }

    if (name == "bellman-ford") {
        return std::make_unique<BellmanFordRouting>();
    }

    throw ConfigError("Unknown routing algorithm: " + name);
}

std::unique_ptr<WeightPolicy> RoutingFactory::makePolicy(const std::string& name) {
    if (name == "hops") {
        return std::make_unique<HopCountWeight>();
    }

    if (name == "latency") {
        return std::make_unique<LatencyWeight>();
    }

    if (name == "latency+congestion") {
        return std::make_unique<LatencyCongestionWeight>(1.0);
    }

    throw ConfigError("Unknown routing weight policy: " + name);
}

}