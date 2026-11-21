#include "netmind/common/Errors.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"
#include "netmind/simulation/EventFactory.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace {

netmind::Graph makeGraph() {
    netmind::Graph graph;

    auto hostA = std::make_unique<netmind::Host>(
        1, "HostA", "10.0.0.1");
    hostA->setGatewayId(2);

    graph.addNode(std::move(hostA));
    graph.addNode(std::make_unique<netmind::Router>(
        2, "R1", "10.0.0.2"));
    graph.addNode(std::make_unique<netmind::Host>(
        3, "HostB", "10.0.0.3"));

    graph.addLink(std::make_unique<netmind::Link>(
        10, 1, 2, 100.0, 1.0, 0.0, 8));
    graph.addLink(std::make_unique<netmind::Link>(
        11, 2, 3, 100.0, 1.0, 0.0, 8));

    return graph;
}

}             

TEST_CASE("EventFactory creates link events from endpoint names") {
    const auto graph = makeGraph();

    const auto failure = netmind::EventFactory::create(
        nlohmann::json{
            {"type", "LINK_FAILURE"},
            {"a", "HostA"},
            {"b", "R1"}
        },
        graph);

    const auto restore = netmind::EventFactory::create(
        nlohmann::json{
            {"type", "LINK_RESTORE"},
            {"a", "R1"},
            {"b", "HostA"}
        },
        graph);

    REQUIRE(failure != nullptr);
    REQUIRE(restore != nullptr);
    REQUIRE(failure->describe().find("LINK_FAILURE") != std::string::npos);
    REQUIRE(restore->describe().find("LINK_RESTORE") != std::string::npos);
}

TEST_CASE("EventFactory creates router events from router names") {
    const auto graph = makeGraph();

    const auto failure = netmind::EventFactory::create(
        nlohmann::json{
            {"type", "ROUTER_FAILURE"},
            {"node", "R1"}
        },
        graph);

    const auto restore = netmind::EventFactory::create(
        nlohmann::json{
            {"type", "ROUTER_RESTORE"},
            {"node", "R1"}
        },
        graph);

    REQUIRE(failure != nullptr);
    REQUIRE(restore != nullptr);
    REQUIRE(failure->describe().find("ROUTER_FAILURE") != std::string::npos);
    REQUIRE(restore->describe().find("ROUTER_RESTORE") != std::string::npos);
}

TEST_CASE("EventFactory creates congestion events from link endpoints") {
    const auto graph = makeGraph();

    const auto event = netmind::EventFactory::create(
        nlohmann::json{
            {"type", "CONGESTION"},
            {"a", "R1"},
            {"b", "HostB"},
            {"bandwidth_multiplier", 0.5},
            {"extra_loss_rate", 0.1},
            {"duration_s", 2.0}
        },
        graph);

    REQUIRE(event != nullptr);
    REQUIRE(event->describe().find("CONGESTION") != std::string::npos);
}

TEST_CASE("EventFactory creates traffic bursts from host names") {
    const auto graph = makeGraph();

    const auto event = netmind::EventFactory::create(
        nlohmann::json{
            {"type", "TRAFFIC_BURST"},
            {"flow_id", 25},
            {"src", "HostA"},
            {"dst", "HostB"},
            {"duration_s", 1.5},
            {"rate_mbps", 8.0},
            {"packet_bytes", 1000}
        },
        graph);

    REQUIRE(event != nullptr);
    REQUIRE(event->describe().find("TRAFFIC_BURST") != std::string::npos);
}

TEST_CASE("EventFactory rejects unknown node and link references") {
    const auto graph = makeGraph();

    REQUIRE_THROWS_AS(
        netmind::EventFactory::create(
            nlohmann::json{
                {"type", "ROUTER_FAILURE"},
                {"node", "MissingRouter"}
            },
            graph),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::EventFactory::create(
            nlohmann::json{
                {"type", "LINK_FAILURE"},
                {"a", "HostA"},
                {"b", "HostB"}
            },
            graph),
        netmind::ConfigError);
}

TEST_CASE("EventFactory rejects node types that do not match the event") {
    const auto graph = makeGraph();

    REQUIRE_THROWS_AS(
        netmind::EventFactory::create(
            nlohmann::json{
                {"type", "ROUTER_FAILURE"},
                {"node", "HostA"}
            },
            graph),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::EventFactory::create(
            nlohmann::json{
                {"type", "TRAFFIC_BURST"},
                {"flow_id", 25},
                {"src", "R1"},
                {"dst", "HostB"},
                {"duration_s", 1.5},
                {"rate_mbps", 8.0},
                {"packet_bytes", 1000}
            },
            graph),
        netmind::ConfigError);
}

TEST_CASE("EventFactory rejects malformed and unsupported events") {
    const auto graph = makeGraph();

    REQUIRE_THROWS_AS(
        netmind::EventFactory::create(nlohmann::json::array(), graph),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::EventFactory::create(
            nlohmann::json{{"a", "HostA"}},
            graph),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::EventFactory::create(
            nlohmann::json{{"type", "UNKNOWN_EVENT"}},
            graph),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::EventFactory::create(
            nlohmann::json{
                {"type", "CONGESTION"},
                {"a", "R1"},
                {"b", "HostB"},
                {"bandwidth_multiplier", 0.5},
                {"extra_loss_rate", 0.1},
                {"duration_s", -2.0}
            },
            graph),
        netmind::ConfigError);
}