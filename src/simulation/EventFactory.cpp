#include "netmind/simulation/EventFactory.h"

#include "netmind/common/Errors.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/simulation/events/CongestionEvent.h"
#include "netmind/simulation/events/LinkFailureEvent.h"
#include "netmind/simulation/events/LinkRestoreEvent"
#include "netmind/simulation/events/RouterFailureEvent.h"
#include "netmind/simulation/events/RouterRestoreEvent.h"
#include "netmind/simulation/events/TrafficBurstEvent.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace netmind {
namespace {

const nlohmann::json& requiredField(const nlohmann::json& object, const char* fieldName) {
    if (!object.contains(fieldName)) {
        throw ConfigError(std::string("Event is missing required field: ") + fieldName);
    }
    return object.at(fieldName);
}

std::string requiredString(const nlohmann::json& object, const char* fieldName) {
    const nlohmann::json& value = requiredField(object, fieldName);
    if (!value.is_string()) {
        throw ConfigError(std::string("Event field must be a string: ") + fieldName);
    }
    return value.get<std::string>();
}

double requiredNumber(const nlohmann::json& object, const char* fieldName) {
    const nlohmann::json& value = requiredField(object, fieldName);
    if (!value.is_number()) {
        throw ConfigError(std::string("Event field must be a number: ") + fieldName);
    }
    const double number = value.get<double>();
    if (!std::isfinite(number)) {
        throw ConfigError(std::string("Event field must be finite: ") + fieldName);
    }
    return number;
}

std::uint32_t requiredUint32(const nlohmann::json& object, const char* fieldName) {
    const nlohmann::json& value = requiredField(object, fieldName);
    const std::uint64_t maxValue = std::numeric_limits<std::uint32_t>::max();
    if (!value.is_number_integer()) {
        throw ConfigError(
            std::string("Event field must be a non-negative integer: ") + fieldName);
    }
    if (value.is_number_unsigned()) {
        const std::uint64_t number = value.get<std::uint64_t>();
        if (number > maxValue) {
            throw ConfigError(std::string("Event integer is too large: ") + fieldName);
        }
        return static_cast<std::uint32_t>(number);
    }
    const std::int64_t number = value.get<std::int64_t>();
    if (number < 0 || static_cast<std::uint64_t>(number) > maxValue) {
        throw ConfigError(
            std::string("Event integer is outside the valid range: ") + fieldName);
    }
    return static_cast<std::uint32_t>(number);
}

const Node& requireNode(const Graph& graph, const std::string& name) {
    const Node* node = graph.findByName(name);
    if (node == nullptr) {
        throw ConfigError("Event refers to unknown node: " + name);
    }
    return *node;
}

LinkId eventLinkId(const nlohmann::json& event, const Graph& graph) {
    const std::string endpointB = requiredString(event, "b");
    const std::string endpointA = requiredString(event, "a");
    const Node& nodeA = requireNode(graph, endpointA);
    const Node& nodeB = requireNode(graph, endpointB);
    const Link* link = graph.linkBetween(nodeA.getId(), nodeB.getId());
    if (link == nullptr) {
        throw ConfigError(
            "Event refers to a link that does not exist between " + endpointA + " and " +
            endpointB);
    }
    return link->getId();
}

NodeId requireNodeOfType(
    const Graph& graph, const std::string& name, NodeType type, const std::string& error) {
    const Node& node = requireNode(graph, name);
    if (node.type() != type) {
        throw ConfigError(error + name);
    }
    return node.getId();
}

NodeId requireRouterId(const nlohmann::json& event, const Graph& graph) {
    return requireNodeOfType(graph, requiredString(event, "node"), NodeType::ROUTER,
                             "Router event refers to a node that is not a router: ");
}

NodeId requireHostId(const nlohmann::json& event, const char* fieldName, const Graph& graph) {
    return requireNodeOfType(graph, requiredString(event, fieldName), NodeType::HOST,
                             "Traffic burst refers to a node that is not a host: ");
}

}

std::shared_ptr<NetworkEvent> EventFactory::create(
    const nlohmann::json& event, const Graph& graph) {
    if (!event.is_object()) {
        throw ConfigError("Each event must be a JSON object");
    }
    const std::string type = requiredString(event, "type");

    if (type == "LINK_FAILURE") {
        return std::make_shared<LinkFailureEvent>(eventLinkId(event, graph));
    }
    if (type == "LINK_RESTORE") {
        return std::make_shared<LinkRestoreEvent>(eventLinkId(event, graph));
    }
    if (type == "ROUTER_FAILURE") {
        return std::make_shared<RouterFailureEvent>(requireRouterId(event, graph));
    }
    if (type == "ROUTER_RESTORE") {
        return std::make_shared<RouterRestoreEvent>(requireRouterId(event, graph));
    }
    if (type == "CONGESTION") {
        const LinkId linkId = eventLinkId(event, graph);
        return std::make_shared<CongestionEvent>(
            linkId,
            requiredNumber(event, "bandwidth_multiplier"),
            requiredNumber(event, "extra_loss_rate"),
            requiredNumber(event, "duration_s"));
    }
    if (type == "TRAFFIC_BURST") {
        const FlowId flowId = requiredUint32(event, "flow_id");
        const NodeId srcId = requireHostId(event, "src", graph);
        const NodeId dstId = requireHostId(event, "dst", graph);
        return std::make_shared<TrafficBurstEvent>(
            flowId,
            srcId,
            dstId,
            requiredNumber(event, "duration_s"),
            requiredNumber(event, "rate_mbps"),
            requiredUint32(event, "packet_bytes"));
    }
    throw ConfigError("Unsupported event type: " + type);
}

}
