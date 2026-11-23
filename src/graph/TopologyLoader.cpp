#include "netmind/graph/TopologyLoader.h"

#include "netmind/common/Errors.h"
#include "netmind/common/Ip.h"
#include "netmind/model/Host.h"
#include "netmind/model/Router.h"
#include "netmind/model/Switch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace netmind {
namespace {

using Json = nlohmann::json;

constexpr std::size_t DEFAULT_QUEUE_CAPACITY = 64;

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
    std::size_t queueCapacity{DEFAULT_QUEUE_CAPACITY};
};

struct SeenNodeValues {
    std::unordered_set<NodeId> ids;
    std::unordered_set<std::string> names;
    std::unordered_set<std::string> ips;
};

struct ParsedTopology {
    std::vector<std::string> errors;
    std::vector<NodeRecord> nodes;
    std::unordered_map<NodeId, std::size_t> nodeIndexById;
    std::vector<LinkRecord> links;
    std::unordered_set<std::uint64_t> seenEdges;
    std::unordered_map<NodeId, std::size_t> hostLinkCounts;
    std::unordered_map<NodeId, NodeId> hostNeighbour;

    [[nodiscard]] bool hasNode(NodeId id) const { return nodeIndexById.count(id) > 0; }
    [[nodiscard]] const NodeRecord& node(NodeId id) const { return nodes[nodeIndexById.at(id)]; }
};

bool addError(std::vector<std::string>& errors, const std::string& message) {
    errors.push_back(message);
    return false;
}

std::string errorSummary(const std::vector<std::string>& errors) {
    std::ostringstream message;
    message << "Invalid topology";
    for (const std::string& error : errors) {
        message << "\n- " << error;
    }
    return message.str();
}

bool readNonNegativeInteger(const Json& object, const char* field, std::uint64_t& result) {
    if (!object.contains(field)) {
        return false;
    }
    const Json& value = object.at(field);
    if (value.is_number_unsigned()) {
        result = value.get<std::uint64_t>();
        return true;
    }
    if (value.is_number_integer() && value.get<std::int64_t>() >= 0) {
        result = static_cast<std::uint64_t>(value.get<std::int64_t>());
        return true;
    }
    return false;
}

bool readNodeId(const Json& object, const char* field, NodeId& result) {
    std::uint64_t value = 0;
    if (!readNonNegativeInteger(object, field, value) || value > std::numeric_limits<NodeId>::max()) {
        return false;
    }
    result = static_cast<NodeId>(value);
    return true;
}

bool readQueueCapacity(const Json& object, std::size_t& result) {
    if (!object.contains("queueCapacity")) {
        result = DEFAULT_QUEUE_CAPACITY;
        return true;
    }
    std::uint64_t value = 0;
    if (!readNonNegativeInteger(object, "queueCapacity", value) ||
        value > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    result = static_cast<std::size_t>(value);
    return true;
}

bool readDouble(const Json& object, const char* field, double& result) {
    if (!object.contains(field) || !object.at(field).is_number()) {
        return false;
    }
    result = object.at(field).get<double>();
    return std::isfinite(result);
}

bool readString(const Json& object, const char* field, std::string& result) {
    if (!object.contains(field) || !object.at(field).is_string()) {
        return false;
    }
    result = object.at(field).get<std::string>();
    return true;
}

std::uint64_t edgeKey(NodeId a, NodeId b) {
    return (static_cast<std::uint64_t>(std::min(a, b)) << 32U) |
           static_cast<std::uint64_t>(std::max(a, b));
}

const Json* findArray(const Json& document, const char* field, std::vector<std::string>& errors) {
    const std::string name = field;
    if (!document.contains(field)) {
        errors.push_back("Missing required field '" + name + "'");
        return nullptr;
    }
    const Json& value = document.at(field);
    if (!value.is_array()) {
        errors.push_back("'" + name + "' must be an array");
        return nullptr;
    }
    return &value;
}

bool checkNodeId(const Json& node, const std::string& prefix, SeenNodeValues& seen,
                 NodeRecord& record, std::vector<std::string>& errors) {
    if (!readNodeId(node, "id", record.id)) {
        return addError(errors, prefix + ".id must be a valid node id");
    }
    if (!seen.ids.insert(record.id).second) {
        return addError(errors, prefix + " has duplicate node id " + std::to_string(record.id));
    }
    return true;
}

bool checkNodeType(const Json& node, const std::string& prefix, NodeRecord& record,
                   std::vector<std::string>& errors) {
    if (!readString(node, "type", record.type)) {
        return addError(errors, prefix + ".type must be a string");
    }
    if (record.type != "HOST" && record.type != "ROUTER" && record.type != "SWITCH") {
        return addError(errors, prefix + ".type must be HOST, ROUTER, or SWITCH");
    }
    return true;
}

bool checkNodeName(const Json& node, const std::string& prefix, SeenNodeValues& seen,
                   NodeRecord& record, std::vector<std::string>& errors) {
    if (!readString(node, "name", record.name)) {
        return addError(errors, prefix + ".name must be a string");
    }
    if (record.name.empty()) {
        return addError(errors, prefix + ".name must not be empty");
    }
    if (!seen.names.insert(record.name).second) {
        return addError(errors, prefix + " has duplicate name '" + record.name + "'");
    }
    return true;
}

bool checkNodeIp(const Json& node, const std::string& prefix, SeenNodeValues& seen,
                 NodeRecord& record, std::vector<std::string>& errors) {
    if (!readString(node, "ip", record.ip)) {
        return addError(errors, prefix + ".ip must be a string");
    }
    if (!isValidIpv4(record.ip)) {
        return addError(errors, prefix + ".ip is not a valid IPv4 address");
    }
    if (!seen.ips.insert(record.ip).second) {
        return addError(errors, prefix + " has duplicate IP address '" + record.ip + "'");
    }
    return true;
}

bool checkNodePosition(const Json& node, const std::string& prefix,
                       std::vector<std::string>& errors) {
    if (!node.contains("pos")) {
        return true;
    }
    const Json& position = node.at("pos");
    const bool validPosition = position.is_array() && position.size() == 2 &&
                               position[0].is_number() && position[1].is_number();
    if (!validPosition) {
        return addError(errors, prefix + ".pos must be an optional two-number array");
    }
    return true;
}

void checkNodes(const Json& nodesJson, ParsedTopology& topology) {
    SeenNodeValues seen;
    for (std::size_t index = 0; index < nodesJson.size(); ++index) {
        const Json& node = nodesJson[index];
        const std::string prefix = "nodes[" + std::to_string(index) + "]";
        if (!node.is_object()) {
            topology.errors.push_back(prefix + " must be an object");
            continue;
        }
        NodeRecord record;
        const bool idOk = checkNodeId(node, prefix, seen, record, topology.errors);
        const bool typeOk = checkNodeType(node, prefix, record, topology.errors);
        const bool nameOk = checkNodeName(node, prefix, seen, record, topology.errors);
        const bool ipOk = checkNodeIp(node, prefix, seen, record, topology.errors);
        const bool positionOk = checkNodePosition(node, prefix, topology.errors);
        if (idOk && typeOk && nameOk && ipOk && positionOk) {
            topology.nodeIndexById.emplace(record.id, topology.nodes.size());
            topology.nodes.push_back(std::move(record));
        }
    }
}

bool checkLinkFields(const Json& link, const std::string& prefix, LinkRecord& record,
                     std::vector<std::string>& errors) {
    bool valid = true;
    if (!readNodeId(link, "a", record.a)) {
        valid = addError(errors, prefix + ".a must be a valid node id");
    }
    if (!readNodeId(link, "b", record.b)) {
        valid = addError(errors, prefix + ".b must be a valid node id");
    }
    if (!readDouble(link, "bandwidthMbps", record.bandwidthMbps)) {
        valid = addError(errors, prefix + ".bandwidthMbps must be a finite number");
    } else if (record.bandwidthMbps <= 0.0) {
        valid = addError(errors, prefix + ".bandwidthMbps must be positive");
    }
    if (!readDouble(link, "latencyMs", record.latencyMs)) {
        valid = addError(errors, prefix + ".latencyMs must be a finite number");
    } else if (record.latencyMs < 0.0) {
        valid = addError(errors, prefix + ".latencyMs must be non-negative");
    }
    if (!readDouble(link, "lossRate", record.lossRate)) {
        valid = addError(errors, prefix + ".lossRate must be a finite number");
    } else if (record.lossRate < 0.0 || record.lossRate > 1.0) {
        valid = addError(errors, prefix + ".lossRate must be in [0, 1]");
    }
    if (!readQueueCapacity(link, record.queueCapacity)) {
        valid = addError(errors, prefix + ".queueCapacity must be a non-negative integer");
    }
    return valid;
}

bool checkLinkEndpoints(const std::string& prefix, const LinkRecord& record,
                        ParsedTopology& topology) {
    bool valid = true;
    if (record.a == record.b) {
        valid = addError(topology.errors, prefix + " cannot connect a node to itself");
    }
    if (!topology.hasNode(record.a) || !topology.hasNode(record.b)) {
        valid = addError(topology.errors, prefix + " refers to a node id that does not exist");
    }
    return valid;
}

void recordHostNeighbours(const LinkRecord& record, ParsedTopology& topology) {
    if (topology.node(record.a).type == "HOST") {
        ++topology.hostLinkCounts[record.a];
        topology.hostNeighbour[record.a] = record.b;
    }
    if (topology.node(record.b).type == "HOST") {
        ++topology.hostLinkCounts[record.b];
        topology.hostNeighbour[record.b] = record.a;
    }
}

void checkLinks(const Json& linksJson, ParsedTopology& topology) {
    for (std::size_t index = 0; index < linksJson.size(); ++index) {
        const Json& link = linksJson[index];
        const std::string prefix = "links[" + std::to_string(index) + "]";
        if (!link.is_object()) {
            topology.errors.push_back(prefix + " must be an object");
            continue;
        }
        LinkRecord record;
        if (!checkLinkFields(link, prefix, record, topology.errors) ||
            !checkLinkEndpoints(prefix, record, topology)) {
            continue;
        }
        if (!topology.seenEdges.insert(edgeKey(record.a, record.b)).second) {
            topology.errors.push_back(prefix + " duplicates a link between the same nodes");
            continue;
        }
        recordHostNeighbours(record, topology);
        topology.links.push_back(record);
    }
}

void checkHostConnections(ParsedTopology& topology) {
    for (const NodeRecord& node : topology.nodes) {
        if (node.type != "HOST") {
            continue;
        }
        if (topology.hostLinkCounts.count(node.id) == 0 || topology.hostLinkCounts.at(node.id) != 1) {
            topology.errors.push_back("Host '" + node.name + "' must have exactly one link");
            continue;
        }
        if (topology.node(topology.hostNeighbour.at(node.id)).type != "ROUTER") {
            topology.errors.push_back("Host '" + node.name + "' must connect to a router");
        }
    }
    if (topology.links.size() > static_cast<std::size_t>(std::numeric_limits<LinkId>::max())) {
        topology.errors.push_back("Topology contains too many links");
    }
}

std::unique_ptr<Node> makeNode(const NodeRecord& node) {
    if (node.type == "HOST") {
        return std::make_unique<Host>(node.id, node.name, node.ip);
    }
    if (node.type == "ROUTER") {
        return std::make_unique<Router>(node.id, node.name, node.ip);
    }
    return std::make_unique<Switch>(node.id, node.name, node.ip);
}

Graph buildGraph(const ParsedTopology& topology) {
    Graph graph;
    for (const NodeRecord& node : topology.nodes) {
        graph.addNode(makeNode(node));
    }
    LinkId linkId = 1;
    for (const LinkRecord& link : topology.links) {
        graph.addLink(std::make_unique<Link>(linkId, link.a, link.b, link.bandwidthMbps,
                                             link.latencyMs, link.lossRate, link.queueCapacity));
        ++linkId;
    }
    for (const NodeRecord& node : topology.nodes) {
        if (node.type == "HOST") {
            auto* host = dynamic_cast<Host*>(graph.getNode(node.id));
            host->setGatewayId(topology.hostNeighbour.at(node.id));
        }
    }
    return graph;
}

}

Graph TopologyLoader::loadFromFile(const std::string& filePath) {
    std::ifstream input(filePath);
    if (!input) {
        throw TopologyError("Could not open topology file: " + filePath);
    }
    nlohmann::json document;
    try {
        input >> document;
    } catch (const nlohmann::json::exception& error) {
        throw TopologyError("Could not parse topology file '" + filePath + "': " + error.what());
    }
    return loadFromJson(document);
}

Graph TopologyLoader::loadFromJson(const nlohmann::json& document) {
    if (!document.is_object()) {
        throw TopologyError("Topology root must be a JSON object");
    }
    ParsedTopology topology;
    const Json* nodesJson = findArray(document, "nodes", topology.errors);
    const Json* linksJson = findArray(document, "links", topology.errors);
    if (nodesJson != nullptr) {
        checkNodes(*nodesJson, topology);
    }
    if (linksJson != nullptr) {
        checkLinks(*linksJson, topology);
    }
    checkHostConnections(topology);
    if (!topology.errors.empty()) {
        throw TopologyError(errorSummary(topology.errors));
    }
    return buildGraph(topology);
}

}
