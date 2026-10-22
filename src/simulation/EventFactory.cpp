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

const nlohmann::json& requiredField(
    const nlohmann::json& object,
    const char* fieldName) {
    if (!object.contains(fieldName)) {
        throw ConfigError(
            std::string("Event is missing required field: ") + fieldName);
    }

    return object.at(fieldName);
}

std::string requiredString(
    const nlohmann::json& object,
    const char* fieldName) {
    const auto& value = requiredField(object, fieldName);

    if (!value.is_string()) {
        throw ConfigError(
            std::string("Event field must be a string: ") + fieldName);
    }

    return value.get<std::string>();
}

double requiredNumber(
    const nlohmann::json& object,
    const char* fieldName) {
    const auto& value = requiredField(object, fieldName);

    if (!value.is_number()) {
        throw ConfigError(
            std::string("Event field must be a number: ") + fieldName);
    }

    const double number = value.get<double>();
    if (!std::isfinite(number)) {
        throw ConfigError(
            std::string("Event field must be finite: ") + fieldName);
    }

    return number;
}

std::uint32_t requiredUint32(
    const nlohmann::json& object,
    const char* fieldName) {
    const auto& value = requiredField(object, fieldName);

    if (!value.is_number_integer()) {
        throw ConfigError(
            std::string("Event field must be a non-negative integer: ") +
            fieldName);
    }

    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > std::numeric_limits<std::uint32_t>::max()) {
            throw ConfigError(
                std::string("Event integer is too large: ") + fieldName);
        }
        return static_cast<std::uint32_t>(number);
    }

    const auto number = value.get<std::int64_t>();
    if (number < 0 ||
        static_cast<std::uint64_t>(number) >
            std::numeric_limits<std::uint32_t>::max()) {
        throw ConfigError(
            std::string("Event integer is outside the valid range: ") +
            fieldName);
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

LinkId requireLinkId(
    const Graph& graph,
    const std::string& endpointA,
    const std::string& endpointB) {
    const Node& nodeA = requireNode(graph, endpointA);
    const Node& nodeB = requireNode(graph, endpointB);

    const Link* link = graph.linkBetween(nodeA.getId(), nodeB.getId());
    if (link == nullptr) {
        throw ConfigError(
            "Event refers to a link that does not exist between " +
            endpointA + " and " + endpointB);
    }

    return link->getId();
}

NodeId requireRouterId(const Graph& graph, const std::string& name) {
    const Node& node = requireNode(graph, name);
    if (node.type() != NodeType::ROUTER) {
        throw ConfigError(
            "Router event refers to a node that is not a router: " + name);
    }

    return node.getId();
}

NodeId requireHostId(const Graph& graph, const std::string& name) {
    const Node& node = requireNode(graph, name);
    if (node.type() != NodeType::HOST) {
        throw ConfigError(
            "Traffic burst refers to a node that is not a host: " + name);
    }

    return node.getId();
}

}             

std::shared_ptr<NetworkEvent> EventFactory::create(
    const nlohmann::json& event,
    const Graph& graph) {
    if (!event.is_object()) {
        throw ConfigError("Each event must be a JSON object");
    }

    const std::string type = requiredString(event, "type");

    if (type == "LINK_FAILURE") {
        const LinkId linkId = requireLinkId(
            graph,
            requiredString(event, "a"),
            requiredString(event, "b"));
        return std::make_shared<LinkFailureEvent>(linkId);
    }

    if (type == "LINK_RESTORE") {
        const LinkId linkId = requireLinkId(
            graph,
            requiredString(event, "a"),
            requiredString(event, "b"));
        return std::make_shared<LinkRestoreEvent>(linkId);
    }

    if (type == "ROUTER_FAILURE") {
        const NodeId routerId =
            requireRouterId(graph, requiredString(event, "node"));
        return std::make_shared<RouterFailureEvent>(routerId);
    }

    if (type == "ROUTER_RESTORE") {
        const NodeId routerId =
            requireRouterId(graph, requiredString(event, "node"));
        return std::make_shared<RouterRestoreEvent>(routerId);
    }

    if (type == "CONGESTION") {
        const LinkId linkId = requireLinkId(
            graph,
            requiredString(event, "a"),
            requiredString(event, "b"));

        return std::make_shared<CongestionEvent>(
            linkId,
            requiredNumber(event, "bandwidth_multiplier"),
            requiredNumber(event, "extra_loss_rate"),
            requiredNumber(event, "duration_s"));
    }

    if (type == "TRAFFIC_BURST") {
        const FlowId flowId = requiredUint32(event, "flow_id");
        const NodeId srcId =
            requireHostId(graph, requiredString(event, "src"));
        const NodeId dstId =
            requireHostId(graph, requiredString(event, "dst"));

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