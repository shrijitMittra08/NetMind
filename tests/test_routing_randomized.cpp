#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "netmind/graph/Graph.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"
#include "netmind/routing/BFSRouting.h"
#include "netmind/routing/BellmanFordRouting.h"
#include "netmind/routing/DFSRouting.h"
#include "netmind/routing/DjikstraRouting.h"
#include "netmind/routing/RoutingAlgorithm.h"
#include "netmind/routing/WeightPolicy.h"

namespace {

using netmind::Graph;
using netmind::NodeId;
using NextHopTable = std::unordered_map<NodeId, NodeId>;
using TablesByRouter = std::unordered_map<NodeId, NextHopTable>;

Graph makeRandomConnectedGraph(std::mt19937_64& random, std::uint64_t graphNumber) {
    Graph graph;

    std::uniform_int_distribution<int> nodeCountDistribution(5, 12);
    const int nodeCount = nodeCountDistribution(random);

    for (int index = 0; index < nodeCount; ++index) {
        const NodeId id = static_cast<NodeId>(index + 1);
        const std::string name = "R" + std::to_string(id);
        const std::string ip =
            "10." + std::to_string(graphNumber % 250 + 1) + "." + std::to_string(id) + ".1";

        graph.addNode(std::make_unique<netmind::Router>(id, name, ip));
    }

    std::vector<std::vector<bool>> connected(
        static_cast<std::size_t>(nodeCount + 1),
        std::vector<bool>(static_cast<std::size_t>(nodeCount + 1), false));

    std::uniform_int_distribution<int> latencyDistribution(1, 20);
    netmind::LinkId nextLinkId = 1;

    for (int child = 2; child <= nodeCount; ++child) {
        std::uniform_int_distribution<int> parentDistribution(1, child - 1);
        const int parent = parentDistribution(random);
        const double latency = static_cast<double>(latencyDistribution(random));

        graph.addLink(std::make_unique<netmind::Link>(nextLinkId++, static_cast<NodeId>(parent),
                                                      static_cast<NodeId>(child), 100.0, latency,
                                                      0.0, 32));

        connected[static_cast<std::size_t>(parent)][static_cast<std::size_t>(child)] = true;
        connected[static_cast<std::size_t>(child)][static_cast<std::size_t>(parent)] = true;
    }

    std::bernoulli_distribution addExtraEdge(0.20);

    for (int first = 1; first <= nodeCount; ++first) {
        for (int second = first + 1; second <= nodeCount; ++second) {
            if (connected[static_cast<std::size_t>(first)][static_cast<std::size_t>(second)] ||
                !addExtraEdge(random)) {
                continue;
            }

            const double latency = static_cast<double>(latencyDistribution(random));

            graph.addLink(std::make_unique<netmind::Link>(nextLinkId++, static_cast<NodeId>(first),
                                                          static_cast<NodeId>(second), 100.0,
                                                          latency, 0.0, 32));

            connected[static_cast<std::size_t>(first)][static_cast<std::size_t>(second)] = true;
            connected[static_cast<std::size_t>(second)][static_cast<std::size_t>(first)] = true;
        }
    }

    return graph;
}

double routeCost(const Graph& graph, const std::vector<NodeId>& path,
                 const netmind::WeightPolicy& policy) {
    double total = 0.0;

    for (std::size_t index = 1; index < path.size(); ++index) {
        const netmind::Link* link = graph.linkBetween(path[index - 1], path[index]);

        REQUIRE(link != nullptr);
        total += policy.weight(*link);
    }

    return total;
}

TablesByRouter makeTables(const Graph& graph, const netmind::RoutingAlgorithm& algorithm,
                          const netmind::WeightPolicy& policy, const std::vector<NodeId>& nodeIds) {
    TablesByRouter tables;

    for (const NodeId routerId : nodeIds) {
        tables.emplace(routerId, algorithm.computeTable(graph, routerId, policy));
    }

    return tables;
}

void requireForwardingRouteReachesDestination(const Graph& graph, const TablesByRouter& tables,
                                              NodeId source, NodeId destination,
                                              std::size_t nodeCount) {
    NodeId current = source;
    std::unordered_set<NodeId> visited;

    for (std::size_t hop = 0; hop < nodeCount; ++hop) {
        if (current == destination) {
            return;
        }

        REQUIRE(visited.insert(current).second);

        const auto currentTable = tables.find(current);
        REQUIRE(currentTable != tables.end());

        const auto route = currentTable->second.find(destination);
        REQUIRE(route != currentTable->second.end());

        const NodeId nextHop = route->second;
        const netmind::Link* link = graph.linkBetween(current, nextHop);

        REQUIRE(link != nullptr);
        REQUIRE(link->isUsable());

        current = nextHop;
    }

    FAIL(
        "Forwarding-table walk did not reach its destination "
        "within the graph's node count");
}

}

TEST_CASE("Routing algorithms agree on seeded random connected graphs") {
    constexpr std::uint64_t seed = 20261011;
    constexpr std::size_t graphCount = 100;

    std::mt19937_64 random(seed);

    netmind::BFSRouting bfs;
    netmind::DFSRouting dfs;
    netmind::DijkstraRouting dijkstra;
    netmind::BellmanFordRouting bellmanFord;
    netmind::LatencyWeight latency;

    for (std::size_t graphIndex = 0; graphIndex < graphCount; ++graphIndex) {
        Graph graph = makeRandomConnectedGraph(random, graphIndex);

        REQUIRE(graph.isConnected());

        const std::vector<NodeId> nodeIds = graph.nodeIds();

        const TablesByRouter bfsTables = makeTables(graph, bfs, latency, nodeIds);
        const TablesByRouter dfsTables = makeTables(graph, dfs, latency, nodeIds);
        const TablesByRouter dijkstraTables = makeTables(graph, dijkstra, latency, nodeIds);
        const TablesByRouter bellmanFordTables = makeTables(graph, bellmanFord, latency, nodeIds);

        for (const NodeId source : nodeIds) {
            for (const NodeId destination : nodeIds) {
                const auto dijkstraPath =
                    dijkstra.computeRoute(graph, source, destination, latency);
                const auto bellmanFordPath =
                    bellmanFord.computeRoute(graph, source, destination, latency);
                const auto bfsPath = bfs.computeRoute(graph, source, destination, latency);
                const auto dfsPath = dfs.computeRoute(graph, source, destination, latency);

                REQUIRE_FALSE(dijkstraPath.empty());
                REQUIRE_FALSE(bellmanFordPath.empty());
                REQUIRE_FALSE(bfsPath.empty());
                REQUIRE_FALSE(dfsPath.empty());

                REQUIRE(routeCost(graph, dijkstraPath, latency) ==
                        routeCost(graph, bellmanFordPath, latency));

                REQUIRE(bfsPath.size() <= dijkstraPath.size());
                REQUIRE(bfsPath.size() <= bellmanFordPath.size());
                REQUIRE(bfsPath.size() <= dfsPath.size());

                if (source != destination) {
                    REQUIRE(dijkstraTables.at(source).find(destination) !=
                            dijkstraTables.at(source).end());
                    REQUIRE(bellmanFordTables.at(source).find(destination) !=
                            bellmanFordTables.at(source).end());
                    REQUIRE(bfsTables.at(source).find(destination) != bfsTables.at(source).end());
                    REQUIRE(dfsTables.at(source).find(destination) != dfsTables.at(source).end());
                }

                INFO("Checking Dijkstra: graph=" << graphIndex << ", source=" << source
                                                 << ", destination=" << destination);
                requireForwardingRouteReachesDestination(graph, dijkstraTables, source, destination,
                                                         nodeIds.size());

                INFO("Checking Bellman-Ford: graph=" << graphIndex << ", source=" << source
                                                     << ", destination=" << destination);
                requireForwardingRouteReachesDestination(graph, bellmanFordTables, source,
                                                         destination, nodeIds.size());

                INFO("Checking BFS: graph=" << graphIndex << ", source=" << source
                                            << ", destination=" << destination);
                requireForwardingRouteReachesDestination(graph, bfsTables, source, destination,
                                                         nodeIds.size());

                INFO("Checking DFS: graph=" << graphIndex << ", source=" << source
                                            << ", destination=" << destination);
                requireForwardingRouteReachesDestination(graph, dfsTables, source, destination,
                                                         nodeIds.size());
            }
        }
    }
}