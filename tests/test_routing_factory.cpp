#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "netmind/common/Errors.h"
#include "netmind/routing/BFSRouting.h"
#include "netmind/routing/BellmanFordRouting.h"
#include "netmind/routing/DFSRouting.h"
#include "netmind/routing/DjikstraRouting.h"
#include "netmind/routing/RoutingFactory.h"

TEST_CASE("RoutingFactory creates each supported algorithm") {
    REQUIRE(dynamic_cast<netmind::BFSRouting*>(
                netmind::RoutingFactory::makeAlgorithm("bfs").get()) != nullptr);

    REQUIRE(dynamic_cast<netmind::DFSRouting*>(
                netmind::RoutingFactory::makeAlgorithm("dfs").get()) != nullptr);

    REQUIRE(dynamic_cast<netmind::DijkstraRouting*>(
                netmind::RoutingFactory::makeAlgorithm("dijkstra").get()) != nullptr);

    REQUIRE(dynamic_cast<netmind::BellmanFordRouting*>(
                netmind::RoutingFactory::makeAlgorithm("bellman-ford").get()) != nullptr);
}

TEST_CASE("RoutingFactory rejects an unknown algorithm name") {
    REQUIRE_THROWS_AS(netmind::RoutingFactory::makeAlgorithm("astar"), netmind::ConfigError);
}

TEST_CASE("RoutingFactory creates each supported weight policy") {
    REQUIRE(dynamic_cast<netmind::HopCountWeight*>(
                netmind::RoutingFactory::makePolicy("hops").get()) != nullptr);

    REQUIRE(dynamic_cast<netmind::LatencyWeight*>(
                netmind::RoutingFactory::makePolicy("latency").get()) != nullptr);

    REQUIRE(dynamic_cast<netmind::LatencyCongestionWeight*>(
                netmind::RoutingFactory::makePolicy("latency+congestion").get()) != nullptr);
}

TEST_CASE("RoutingFactory rejects an unknown weight policy name") {
    REQUIRE_THROWS_AS(netmind::RoutingFactory::makePolicy("throughput"), netmind::ConfigError);
}