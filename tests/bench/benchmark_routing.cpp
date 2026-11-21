#include "netmind/graph/Graph.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"
#include "netmind/routing/BFSRouting.h"
#include "netmind/routing/BellmanFordRouting.h"
#include "netmind/routing/DFSRouting.h"
#include "netmind/routing/DjikstraRouting.h"
#include "netmind/routing/RoutingAlgorithm.h"
#include "netmind/routing/WeightPolicy.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using netmind::Graph;
using netmind::NodeId;

constexpr std::uint64_t kSeed = 20261011;
constexpr std::size_t kRepetitions = 3;

std::uint64_t edgeKey(NodeId first, NodeId second) {
    if (first > second) {
        std::swap(first, second);
    }

    return (static_cast<std::uint64_t>(first) << 32) |
           static_cast<std::uint64_t>(second);
}

Graph makeBenchmarkGraph(std::size_t nodeCount, std::uint64_t seed) {
    Graph graph;
    std::mt19937_64 random(seed);
    std::uniform_int_distribution<int> latencyDistribution(1, 20);

    for (std::size_t index = 1; index <= nodeCount; ++index) {
        const NodeId id = static_cast<NodeId>(index);
        const std::string name = "R" + std::to_string(id);

                                                                         
        const std::string ip =
            "10.0." + std::to_string(id / 250) + "." +
            std::to_string(id % 250 + 1);

        graph.addNode(
            std::make_unique<netmind::Router>(id, name, ip));
    }

    std::unordered_set<std::uint64_t> existingEdges;
    existingEdges.reserve(nodeCount * 5);

    netmind::LinkId nextLinkId = 1;

    auto addLink = [&](NodeId first, NodeId second) {
        const std::uint64_t key = edgeKey(first, second);

        if (!existingEdges.insert(key).second) {
            return false;
        }

        const double latency =
            static_cast<double>(latencyDistribution(random));

        graph.addLink(std::make_unique<netmind::Link>(
            nextLinkId++,
            first,
            second,
            1000.0,
            latency,
            0.0,
            64));

        return true;
    };

                                                      
    for (NodeId child = 2;
         child <= static_cast<NodeId>(nodeCount);
         ++child) {
        std::uniform_int_distribution<NodeId> parentDistribution(
            1, child - 1);

        addLink(parentDistribution(random), child);
    }

                                                                    
                                                                       
    const std::size_t targetLinkCount = nodeCount * 4;
    std::uniform_int_distribution<NodeId> nodeDistribution(
        1, static_cast<NodeId>(nodeCount));

    while (existingEdges.size() < targetLinkCount) {
        const NodeId first = nodeDistribution(random);
        const NodeId second = nodeDistribution(random);

        if (first == second) {
            continue;
        }

        addLink(first, second);
    }

    return graph;
}

std::uint64_t tableChecksum(
    const std::unordered_map<NodeId, NodeId>& table
) {
    std::uint64_t checksum = 0;

    for (const auto& entry : table) {
        checksum +=
            static_cast<std::uint64_t>(entry.first) * 31 +
            static_cast<std::uint64_t>(entry.second);
    }

    return checksum;
}

struct Algorithm {
    std::string name;
    const netmind::RoutingAlgorithm* implementation;
};

}             

int main() {
    try {
        const std::filesystem::path outputPath =
            std::filesystem::path("data") / "bench.csv";

        std::filesystem::create_directories(outputPath.parent_path());

        std::ofstream output(outputPath);
        if (!output) {
            throw std::runtime_error(
                "Could not open data/bench.csv for writing");
        }

        output
            << "algorithm,node_count,link_count,seed,repetition,"
               "microseconds,route_entries,checksum\n";

        netmind::BFSRouting bfs;
        netmind::DijkstraRouting dijkstra;
        netmind::BellmanFordRouting bellmanFord;
        netmind::DFSRouting dfs;
        netmind::LatencyWeight latency;

        const std::array<Algorithm, 4> algorithms{{
            {"BFS", &bfs},
            {"Dijkstra", &dijkstra},
            {"Bellman-Ford", &bellmanFord},
            {"DFS", &dfs}
        }};

        constexpr std::array<std::size_t, 5> graphSizes{{
            100, 500, 1000, 2000, 5000
        }};

        for (const std::size_t nodeCount : graphSizes) {
            const std::uint64_t graphSeed =
                kSeed + static_cast<std::uint64_t>(nodeCount);

            Graph graph = makeBenchmarkGraph(nodeCount, graphSeed);

            std::cout << "Benchmarking " << nodeCount
                      << " nodes and " << graph.numLinks()
                      << " links...\n";

            for (const Algorithm& algorithm : algorithms) {
                for (std::size_t repetition = 1;
                     repetition <= kRepetitions;
                     ++repetition) {
                    const auto start =
                        std::chrono::steady_clock::now();

                    const auto table =
                        algorithm.implementation->computeTable(
                            graph, 1, latency);

                    const auto finish =
                        std::chrono::steady_clock::now();

                    const auto elapsed =
                        std::chrono::duration<double, std::micro>(
                            finish - start).count();

                    if (table.size() != nodeCount - 1) {
                        throw std::runtime_error(
                            algorithm.name +
                            " did not produce a route for every "
                            "reachable destination");
                    }

                    output
                        << algorithm.name << ','
                        << nodeCount << ','
                        << graph.numLinks() << ','
                        << graphSeed << ','
                        << repetition << ','
                        << std::fixed << std::setprecision(2)
                        << elapsed << ','
                        << table.size() << ','
                        << tableChecksum(table) << '\n';

                    output.flush();

                    std::cout << "  " << algorithm.name
                              << ", run " << repetition
                              << ": " << std::fixed
                              << std::setprecision(2)
                              << elapsed << " microseconds\n";
                }
            }
        }

        std::cout << "Results saved to "
                  << outputPath.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: "
                  << error.what() << '\n';
        return 1;
    }
}