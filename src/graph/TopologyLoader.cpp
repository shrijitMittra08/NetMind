#include "netmind/graph/TopologyLoader.h"

#include "netmind/common/Errors.h"
#include "netmind/common/Ip.h"
#include "netmind/model/Host.h"
#include "netmind/model/Router.h"
#include "netmind/model/Switch.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace netmind {
namespace {

struct NodeRecord {
    NodeId id{};
    std::string type;
    std::string name;
    std::string ip;
};

struct LinkRecord {
    NodeId a{};
    NodeId b{};
    double bandwidthMbps{};
    double latencyMs{};
    double lossRate{};
    std::size_t queueCapacity{64};
};

void addError(
    std::vector<std::string>& errors,
    const std::string& message
) {
    errors.push_back(message);
}

std::string errorSummary(const std::vector<std::string>& errors) {
    std::ostringstream message;
    message << "Invalid topology";

    for (const std::string& error : errors) {
        message << "\n- " << error;
    }

    return message.str();
}

bool readNodeId(
    const nlohmann::json& object,
    const char* field,
    NodeId& result
) {
    const auto it = object.find(field);
    if (it == object.end()) {
        return false;
    }

    std::uint64_t value = 0;

    try {
        if (it->is_number_unsigned()) {
            value = it->get<std::uint64_t>();
        } else if (it->is_number_integer()) {
            const std::int64_t signedValue = it->get<std::int64_t>();
            if (signedValue < 0) {
                return false;
            }
            value = static_cast<std::uint64_t>(signedValue);
        } else {
            return false;
        }
    } catch (const nlohmann::json::exception&) {
        return false;
    }

    if (value > std::numeric_limits<NodeId>::max()) {
        return false;
    }

    result = static_cast<NodeId>(value);
    return true;
}

bool readDouble(
    const nlohmann::json& object,
    const char* field,
    double& result
) {
    const auto it = object.find(field);
    if (it == object.end() || !it->is_number()) {
        return false;
    }

    try {
        result = it->get<double>();
    } catch (const nlohmann::json::exception&) {
        return false;
    }

    return std::isfinite(result);
}

bool readQueueCapacity(
    const nlohmann::json& object,
    std::size_t& result
) {
    const auto it = object.find("queueCapacity");
    if (it == object.end()) {
        result = 64;
        return true;
    }

    std::uint64_t value = 0;

    try {
        if (it->is_number_unsigned()) {
            value = it->get<std::uint64_t>();
        } else if (it->is_number_integer()) {
            const std::int64_t signedValue = it->get<std::int64_t>();
            if (signedValue < 0) {
                return false;
            }
            value = static_cast<std::uint64_t>(signedValue);
        } else {
            return false;
        }
    } catch (const nlohmann::json::exception&) {
        return false;
    }

    if (value > std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    result = static_cast<std::size_t>(value);
    return true;
}

bool validNodeType(const std::string& type) {
    return type == "HOST" || type == "ROUTER" || type == "SWITCH";
}

std::uint64_t edgeKey(NodeId a, NodeId b) {
    const NodeId low = a < b ? a : b;
    const NodeId high = a < b ? b : a;

    return (static_cast<std::uint64_t>(low) << 32U) |
           static_cast<std::uint64_t>(high);
}

}             

Graph TopologyLoader::loadFromFile(const std::string& filePath) {
    std::ifstream input(filePath);
    if (!input) {
        throw TopologyError(
            "Could not open topology file: " + filePath);
    }

    nlohmann::json document;

    try {
        input >> document;
    } catch (const nlohmann::json::exception& error) {
        throw TopologyError(
            "Could not parse topology file '" + filePath +
            "': " + error.what());
    }

    return loadFromJson(document);
}

Graph TopologyLoader::loadFromJson(const nlohmann::json& document) {
    std::vector<std::string> errors;

    if (!document.is_object()) {
        throw TopologyError("Topology root must be a JSON object");
    }

    const auto nodesIt = document.find("nodes");
    const auto linksIt = document.find("links");

    const nlohmann::json* nodesJson = nullptr;
    const nlohmann::json* linksJson = nullptr;

    if (nodesIt == document.end()) {
        addError(errors, "Missing required field 'nodes'");
    } else if (!nodesIt->is_array()) {
        addError(errors, "'nodes' must be an array");
    } else {
        nodesJson = &*nodesIt;
    }

    if (linksIt == document.end()) {
        addError(errors, "Missing required field 'links'");
    } else if (!linksIt->is_array()) {
        addError(errors, "'links' must be an array");
    } else {
        linksJson = &*linksIt;
    }

    std::vector<NodeRecord> nodeRecords;
    std::unordered_map<NodeId, std::size_t> nodeRecordById;
    std::unordered_set<NodeId> nodeIds;
    std::unordered_set<std::string> nodeNames;
    std::unordered_set<std::string> nodeIps;

    if (nodesJson != nullptr) {
        for (std::size_t index = 0; index < nodesJson->size(); ++index) {
            const nlohmann::json& node = (*nodesJson)[index];
            const std::string prefix =
                "nodes[" + std::to_string(index) + "]";

            if (!node.is_object()) {
                addError(errors, prefix + " must be an object");
                continue;
            }

            bool valid = true;
            NodeRecord record;

            if (!readNodeId(node, "id", record.id)) {
                addError(errors, prefix + ".id must be a valid node id");
                valid = false;
            } else if (!nodeIds.insert(record.id).second) {
                addError(
                    errors,
                    prefix + " has duplicate node id " +
                    std::to_string(record.id));
                valid = false;
            }

            const auto typeIt = node.find("type");
            if (typeIt == node.end() || !typeIt->is_string()) {
                addError(errors, prefix + ".type must be a string");
                valid = false;
            } else {
                record.type = typeIt->get<std::string>();
                if (!validNodeType(record.type)) {
                    addError(
                        errors,
                        prefix + ".type must be HOST, ROUTER, or SWITCH");
                    valid = false;
                }
            }

            const auto nameIt = node.find("name");
            if (nameIt == node.end() || !nameIt->is_string()) {
                addError(errors, prefix + ".name must be a string");
                valid = false;
            } else {
                record.name = nameIt->get<std::string>();
                if (record.name.empty()) {
                    addError(errors, prefix + ".name must not be empty");
                    valid = false;
                } else if (!nodeNames.insert(record.name).second) {
                    addError(
                        errors,
                        prefix + " has duplicate name '" + record.name + "'");
                    valid = false;
                }
            }

            const auto ipIt = node.find("ip");
            if (ipIt == node.end() || !ipIt->is_string()) {
                addError(errors, prefix + ".ip must be a string");
                valid = false;
            } else {
                record.ip = ipIt->get<std::string>();
                if (!isValidIpv4(record.ip)) {
                    addError(
                        errors,
                        prefix + ".ip is not a valid IPv4 address");
                    valid = false;
                } else if (!nodeIps.insert(record.ip).second) {
                    addError(
                        errors,
                        prefix + " has duplicate IP address '" +
                        record.ip + "'");
                    valid = false;
                }
            }

                                                                    
            const auto positionIt = node.find("pos");
            if (positionIt != node.end()) {
                bool validPosition =
                    positionIt->is_array() &&
                    positionIt->size() == 2;

                if (validPosition) {
                    validPosition =
                        (*positionIt)[0].is_number() &&
                        (*positionIt)[1].is_number();
                }

                if (!validPosition) {
                    addError(
                        errors,
                        prefix + ".pos must be an optional two-number array");
                    valid = false;
                }
            }

            if (valid) {
                nodeRecordById.emplace(record.id, nodeRecords.size());
                nodeRecords.push_back(std::move(record));
            }
        }
    }

    std::vector<LinkRecord> linkRecords;
    std::unordered_map<NodeId, std::size_t> hostLinkCounts;
    std::unordered_map<NodeId, NodeId> hostNeighbor;
    std::unordered_set<std::uint64_t> seenEdges;

    if (linksJson != nullptr) {
        for (std::size_t index = 0; index < linksJson->size(); ++index) {
            const nlohmann::json& link = (*linksJson)[index];
            const std::string prefix =
                "links[" + std::to_string(index) + "]";

            if (!link.is_object()) {
                addError(errors, prefix + " must be an object");
                continue;
            }

            bool valid = true;
            LinkRecord record;

            if (!readNodeId(link, "a", record.a)) {
                addError(errors, prefix + ".a must be a valid node id");
                valid = false;
            }

            if (!readNodeId(link, "b", record.b)) {
                addError(errors, prefix + ".b must be a valid node id");
                valid = false;
            }

            if (!readDouble(link, "bandwidthMbps", record.bandwidthMbps)) {
                addError(
                    errors,
                    prefix + ".bandwidthMbps must be a finite number");
                valid = false;
            } else if (record.bandwidthMbps <= 0.0) {
                addError(errors, prefix + ".bandwidthMbps must be positive");
                valid = false;
            }

            if (!readDouble(link, "latencyMs", record.latencyMs)) {
                addError(
                    errors,
                    prefix + ".latencyMs must be a finite number");
                valid = false;
            } else if (record.latencyMs < 0.0) {
                addError(errors, prefix + ".latencyMs must be non-negative");
                valid = false;
            }

            if (!readDouble(link, "lossRate", record.lossRate)) {
                addError(
                    errors,
                    prefix + ".lossRate must be a finite number");
                valid = false;
            } else if (record.lossRate < 0.0 || record.lossRate > 1.0) {
                addError(errors, prefix + ".lossRate must be in [0, 1]");
                valid = false;
            }

            if (!readQueueCapacity(link, record.queueCapacity)) {
                addError(
                    errors,
                    prefix + ".queueCapacity must be a non-negative integer");
                valid = false;
            }

            if (valid) {
                if (record.a == record.b) {
                    addError(errors, prefix + " cannot connect a node to itself");
                    valid = false;
                }

                if (nodeRecordById.find(record.a) == nodeRecordById.end() ||
                    nodeRecordById.find(record.b) == nodeRecordById.end()) {
                    addError(
                        errors,
                        prefix + " refers to a node id that does not exist");
                    valid = false;
                }
            }

            if (valid) {
                const std::uint64_t key = edgeKey(record.a, record.b);
                if (!seenEdges.insert(key).second) {
                    addError(
                        errors,
                        prefix + " duplicates a link between the same nodes");
                    valid = false;
                }
            }

            if (!valid) {
                continue;
            }

            const NodeRecord& nodeA =
                nodeRecords[nodeRecordById.at(record.a)];
            const NodeRecord& nodeB =
                nodeRecords[nodeRecordById.at(record.b)];

            if (nodeA.type == "HOST") {
                ++hostLinkCounts[record.a];
                hostNeighbor[record.a] = record.b;
            }
            if (nodeB.type == "HOST") {
                ++hostLinkCounts[record.b];
                hostNeighbor[record.b] = record.a;
            }

            linkRecords.push_back(record);
        }
    }

    for (const NodeRecord& node : nodeRecords) {
        if (node.type != "HOST") {
            continue;
        }

        const std::size_t count = hostLinkCounts[node.id];
        if (count != 1) {
            addError(
                errors,
                "Host '" + node.name + "' must have exactly one link");
            continue;
        }

        const NodeId neighborId = hostNeighbor.at(node.id);
        const NodeRecord& neighbor =
            nodeRecords[nodeRecordById.at(neighborId)];

        if (neighbor.type != "ROUTER") {
            addError(
                errors,
                "Host '" + node.name + "' must connect to a router");
        }
    }

    if (linkRecords.size() >
        static_cast<std::size_t>(std::numeric_limits<LinkId>::max())) {
        addError(errors, "Topology contains too many links");
    }

    if (!errors.empty()) {
        throw TopologyError(errorSummary(errors));
    }

    Graph graph;

    for (const NodeRecord& node : nodeRecords) {
        if (node.type == "HOST") {
            graph.addNode(
                std::make_unique<Host>(node.id, node.name, node.ip));
        } else if (node.type == "ROUTER") {
            graph.addNode(
                std::make_unique<Router>(node.id, node.name, node.ip));
        } else {
            graph.addNode(
                std::make_unique<Switch>(node.id, node.name, node.ip));
        }
    }

    LinkId linkId = 1;
    for (const LinkRecord& link : linkRecords) {
        graph.addLink(std::make_unique<Link>(
            linkId++,
            link.a,
            link.b,
            link.bandwidthMbps,
            link.latencyMs,
            link.lossRate,
            link.queueCapacity));
    }

    for (const NodeRecord& node : nodeRecords) {
        if (node.type == "HOST") {
            auto* host = dynamic_cast<Host*>(graph.getNode(node.id));
            host->setGatewayId(hostNeighbor.at(node.id));
        }
    }

    return graph;
}

}