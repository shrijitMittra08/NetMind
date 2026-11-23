#include "netmind/cli/commands/TopologyCommands.h"

#include "netmind/cli/CommandLineInterface.h"
#include "netmind/cli/Session.h"
#include "netmind/common/Errors.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Link.h"
#include "netmind/model/Node.h"
#include "netmind/simulation/ScenarioManager.h"
#include "netmind/simulation/SimulationEngine.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace netmind::cli {
namespace {

std::string nodeTypeName(NodeType type) {
    switch (type) {
        case NodeType::HOST: return "HOST";
        case NodeType::ROUTER: return "ROUTER";
        case NodeType::SWITCH: return "SWITCH";
    }
    return "UNKNOWN";
}

std::string linkStateName(LinkState state) {
    switch (state) {
        case LinkState::UP: return "UP";
        case LinkState::DOWN: return "DOWN";
        case LinkState::DEGRADED: return "DEGRADED";
    }
    return "UNKNOWN";
}

std::string neighborNames(const Graph& graph, NodeId nodeId) {
    std::vector<std::string> names;
    for (const Link* link : graph.getNeighbours(nodeId)) {
        if (const Node* neighbor = graph.getNode(link->other(nodeId))) {
            names.push_back(neighbor->name());
        }
    }
    if (names.empty()) {
        return "-";
    }
    std::sort(names.begin(), names.end());
    std::string result = names[0];
    for (std::size_t i = 1; i < names.size(); ++i) {
        result += ", " + names[i];
    }
    return result;
}

std::vector<const Link*> linksInDisplayOrder(const Graph& graph) {
    std::vector<const Link*> links;
    std::set<LinkId> seen;
    for (NodeId nodeId : graph.nodeIds()) {
        for (const Link* link : graph.getNeighbours(nodeId)) {
            if (link == nullptr || !seen.insert(link->getId()).second) {
                continue;
            }
            if (graph.getNode(link->a()) != nullptr && graph.getNode(link->b()) != nullptr) {
                links.push_back(link);
            }
        }
    }
    return links;
}

}

std::string LoadCommand::name() const { return "load"; }
std::string LoadCommand::help() const { return "Load a topology or scenario JSON file"; }

void LoadCommand::execute(Session& session, const std::vector<std::string>& args,
                          std::ostream& output) {
    if (args.size() != 1) {
        throw std::invalid_argument("Usage: load <file>");
    }
    const std::string& path = args.front();
    std::ifstream input(path);
    if (!input) {
        throw ConfigError("Could not open input file: " + path);
    }
    nlohmann::json document;
    try {
        input >> document;
    } catch (const nlohmann::json::exception& error) {
        throw ConfigError("Could not parse JSON file '" + path + "': " + error.what());
    }

    const bool isScenario = document.is_object() &&
                            (document.contains("topology") || document.contains("duration_s") ||
                             document.contains("routing") || document.contains("flows"));
    ScenarioManager candidate;
    if (isScenario) {
        candidate.loadScenario(path);
    } else {
        candidate.loadTopology(path);
    }
    const std::size_t nodeCount = candidate.graph().numNodes();
    const std::size_t linkCount = candidate.graph().numLinks();

    session.clearRuntime();
    session.scenarioManager() = std::move(candidate);
    output << "Loaded " << (isScenario ? "scenario" : "topology") << ": " << nodeCount
           << " nodes, " << linkCount << " links\n";
}

std::string ShowTopologyCommand::name() const { return "show topology"; }
std::string ShowTopologyCommand::help() const {
    return "Display nodes, neighbors, and link properties";
}

void ShowTopologyCommand::execute(Session& session, const std::vector<std::string>& args,
                                  std::ostream& output) {
    if (!args.empty()) {
        throw std::invalid_argument("Usage: show topology");
    }
    const Graph& graph =
        session.engine() != nullptr ? session.engine()->graph() : session.scenarioManager().graph();
    const std::vector<const Link*> links = linksInDisplayOrder(graph);

    output << "Topology: " << graph.numNodes() << " nodes, " << graph.numLinks() << " links\n\n";
    output << "Nodes:\n";
    output << std::left << std::setw(16) << "Name" << std::setw(12) << "Type" << std::setw(18)
           << "IP" << "Neighbors\n";
    for (NodeId nodeId : graph.nodeIds()) {
        if (const Node* node = graph.getNode(nodeId)) {
            output << std::left << std::setw(16) << node->name() << std::setw(12)
                   << nodeTypeName(node->type()) << std::setw(18) << node->ip()
                   << neighborNames(graph, nodeId) << '\n';
        }
    }

    output << "\nGraph view (one edge per link):\n";
    for (const Link* link : links) {
        const Node* endpointA = graph.getNode(link->a());
        const Node* endpointB = graph.getNode(link->b());
        output << "  [" << endpointA->name() << " (" << nodeTypeName(endpointA->type()) << ")]"
               << " ---- "
               << "[" << endpointB->name() << " (" << nodeTypeName(endpointB->type()) << ")]"
               << "  link " << link->getId() << ", " << std::fixed << std::setprecision(2)
               << link->bandwidthMbps() << " Mbps, " << link->latencyMs() << " ms, "
               << linkStateName(link->state()) << '\n';
    }

    output << "\nLinks:\n";
    output << std::left << std::setw(6) << "ID" << std::setw(22) << "Endpoints" << std::right
           << std::setw(14) << "Mbps" << std::setw(14) << "Latency ms" << std::setw(12) << "Loss"
           << "  " << std::left << "State\n";
    for (const Link* link : links) {
        const std::string endpoints =
            graph.getNode(link->a())->name() + " <-> " + graph.getNode(link->b())->name();
        output << std::left << std::setw(6) << link->getId() << std::setw(22) << endpoints
               << std::right << std::fixed << std::setprecision(2) << std::setw(14)
               << link->bandwidthMbps() << std::setw(14) << link->latencyMs() << std::setw(12)
               << link->lossRate() << "  " << std::left << linkStateName(link->state()) << '\n';
    }
}

void registerTopologyCommands(CommandLineInterface& cli) {
    cli.addCommand(std::make_unique<LoadCommand>());
    cli.addCommand(std::make_unique<ShowTopologyCommand>());
}

}
