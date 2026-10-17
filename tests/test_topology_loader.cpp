#include "netmind/common/Errors.h"
#include "netmind/graph/TopologyLoader.h"
#include "netmind/model/Host.h"
#include "netmind/model/Router.h"
#include "netmind/model/Switch.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

nlohmann::json validTopology() {
    return {
        {"nodes", {
            {
                {"id", 1},
                {"type", "HOST"},
                {"name", "PC1"},
                {"ip", "10.0.0.1"}
            },
            {
                {"id", 2},
                {"type", "ROUTER"},
                {"name", "R1"},
                {"ip", "10.0.0.254"}
            },
            {
                {"id", 3},
                {"type", "HOST"},
                {"name", "PC2"},
                {"ip", "10.0.1.1"}
            }
        }},
        {"links", {
            {
                {"a", 1},
                {"b", 2},
                {"bandwidthMbps", 100},
                {"latencyMs", 2},
                {"lossRate", 0.0}
            },
            {
                {"a", 2},
                {"b", 3},
                {"bandwidthMbps", 50},
                {"latencyMs", 4},
                {"lossRate", 0.01},
                {"queueCapacity", 32}
            }
        }}
    };
}

} // namespace

TEST_CASE("TopologyLoader builds nodes, links, and host gateways") {
    const netmind::Graph graph =
        netmind::TopologyLoader::loadFromJson(validTopology());

    REQUIRE(graph.numNodes() == 3);
    REQUIRE(graph.numLinks() == 2);
    REQUIRE(graph.isConnected());

    const auto* host1 = dynamic_cast<const netmind::Host*>(graph.getNode(1));
    const auto* router = dynamic_cast<const netmind::Router*>(graph.getNode(2));
    const auto* host2 = dynamic_cast<const netmind::Host*>(graph.getNode(3));

    REQUIRE(host1 != nullptr);
    REQUIRE(router != nullptr);
    REQUIRE(host2 != nullptr);

    REQUIRE(host1->gatewayId().has_value());
    REQUIRE(*host1->gatewayId() == 2);
    REQUIRE(host2->gatewayId().has_value());
    REQUIRE(*host2->gatewayId() == 2);

    REQUIRE(graph.linkBetween(1, 2)->bandwidthMbps() == 100.0);
    REQUIRE(graph.linkBetween(2, 3)->queueCapacity() == 32);
}

TEST_CASE("TopologyLoader creates Switch nodes") {
    nlohmann::json document = {
        {"nodes", {
            {
                {"id", 1},
                {"type", "HOST"},
                {"name", "PC1"},
                {"ip", "10.0.0.1"}
            },
            {
                {"id", 2},
                {"type", "ROUTER"},
                {"name", "R1"},
                {"ip", "10.0.0.2"}
            },
            {
                {"id", 3},
                {"type", "SWITCH"},
                {"name", "S1"},
                {"ip", "10.0.0.3"}
            }
        }},
        {"links", {
            {
                {"a", 1},
                {"b", 2},
                {"bandwidthMbps", 100},
                {"latencyMs", 1},
                {"lossRate", 0.0}
            },
            {
                {"a", 2},
                {"b", 3},
                {"bandwidthMbps", 100},
                {"latencyMs", 1},
                {"lossRate", 0.0}
            }
        }}
    };

    const netmind::Graph graph =
        netmind::TopologyLoader::loadFromJson(document);

    REQUIRE(dynamic_cast<const netmind::Switch*>(graph.getNode(3)) != nullptr);
}

TEST_CASE("TopologyLoader applies the default queue capacity") {
    const netmind::Graph graph =
        netmind::TopologyLoader::loadFromJson(validTopology());

    REQUIRE(graph.linkBetween(1, 2)->queueCapacity() == 64);
}

TEST_CASE("TopologyLoader accepts disconnected graphs") {
    nlohmann::json document = {
        {"nodes", {
            {
                {"id", 1},
                {"type", "HOST"},
                {"name", "PC1"},
                {"ip", "10.0.0.1"}
            },
            {
                {"id", 2},
                {"type", "ROUTER"},
                {"name", "R1"},
                {"ip", "10.0.0.2"}
            },
            {
                {"id", 3},
                {"type", "HOST"},
                {"name", "PC2"},
                {"ip", "10.0.0.3"}
            },
            {
                {"id", 4},
                {"type", "ROUTER"},
                {"name", "R2"},
                {"ip", "10.0.0.4"}
            }
        }},
        {"links", {
            {
                {"a", 1},
                {"b", 2},
                {"bandwidthMbps", 100},
                {"latencyMs", 1},
                {"lossRate", 0.0}
            },
            {
                {"a", 3},
                {"b", 4},
                {"bandwidthMbps", 100},
                {"latencyMs", 1},
                {"lossRate", 0.0}
            }
        }}
    };

    const netmind::Graph graph =
        netmind::TopologyLoader::loadFromJson(document);

    REQUIRE(graph.numNodes() == 4);
    REQUIRE(graph.numLinks() == 2);
    REQUIRE_FALSE(graph.isConnected());
    REQUIRE(graph.connectedComponents().size() == 2);
}

TEST_CASE("TopologyLoader aggregates invalid node and link errors") {
    nlohmann::json document = {
        {"nodes", {
            {
                {"id", 1},
                {"type", "HOST"},
                {"name", "PC1"},
                {"ip", "10.0.0.1"}
            },
            {
                {"id", 2},
                {"type", "ALIEN"},
                {"name", "PC1"},
                {"ip", "999.0.0.1"}
            }
        }},
        {"links", {
            {
                {"a", 1},
                {"b", 99},
                {"bandwidthMbps", 0},
                {"latencyMs", -1},
                {"lossRate", 2.0}
            }
        }}
    };

    try {
        (void)netmind::TopologyLoader::loadFromJson(document);
        FAIL("Expected invalid topology to throw");
    } catch (const netmind::TopologyError& error) {
        const std::string message = error.what();
        REQUIRE(message.find("type") != std::string::npos);
        REQUIRE(message.find("duplicate name") != std::string::npos);
        REQUIRE(message.find("IPv4") != std::string::npos);
        REQUIRE(message.find("bandwidthMbps") != std::string::npos);
        REQUIRE(message.find("latencyMs") != std::string::npos);
        REQUIRE(message.find("lossRate") != std::string::npos);
    }
}

TEST_CASE("TopologyLoader rejects hosts without exactly one router link") {
    SECTION("host has no links") {
        nlohmann::json document = {
            {"nodes", {
                {
                    {"id", 1},
                    {"type", "HOST"},
                    {"name", "PC1"},
                    {"ip", "10.0.0.1"}
                }
            }},
            {"links", nlohmann::json::array()}
        };

        REQUIRE_THROWS_AS(
            netmind::TopologyLoader::loadFromJson(document),
            netmind::TopologyError
        );
    }

    SECTION("host is connected to another host") {
        nlohmann::json document = {
            {"nodes", {
                {
                    {"id", 1},
                    {"type", "HOST"},
                    {"name", "PC1"},
                    {"ip", "10.0.0.1"}
                },
                {
                    {"id", 2},
                    {"type", "HOST"},
                    {"name", "PC2"},
                    {"ip", "10.0.0.2"}
                }
            }},
            {"links", {
                {
                    {"a", 1},
                    {"b", 2},
                    {"bandwidthMbps", 100},
                    {"latencyMs", 1},
                    {"lossRate", 0.0}
                }
            }}
        };

        REQUIRE_THROWS_AS(
            netmind::TopologyLoader::loadFromJson(document),
            netmind::TopologyError
        );
    }
}

TEST_CASE("TopologyLoader rejects malformed root structure") {
    REQUIRE_THROWS_AS(
        netmind::TopologyLoader::loadFromJson(nlohmann::json::array()),
        netmind::TopologyError
    );

    REQUIRE_THROWS_AS(
        netmind::TopologyLoader::loadFromJson(nlohmann::json::object()),
        netmind::TopologyError
    );
}