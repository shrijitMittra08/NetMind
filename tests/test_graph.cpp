#include "netmind/common/Errors.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/Router.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace {

std::unique_ptr<netmind::Link> makeLink(
    netmind::LinkId id,
    netmind::NodeId a,
    netmind::NodeId b
) {
    return std::make_unique<netmind::Link>(
        id, a, b, 100.0, 1.0, 0.0, 16);
}

} // namespace

TEST_CASE("Graph adds nodes and looks them up by id and name") {
    netmind::Graph graph;
    graph.addNode(
        std::make_unique<netmind::Host>(1, "HostA", "10.0.0.1"));
    graph.addNode(
        std::make_unique<netmind::Router>(2, "RouterA", "10.0.0.2"));

    REQUIRE(graph.numNodes() == 2);
    REQUIRE(graph.getNode(1) != nullptr);
    REQUIRE(graph.getNode(1)->name() == "HostA");
    REQUIRE(graph.getNode(99) == nullptr);
    REQUIRE(graph.findByName("RouterA") != nullptr);
    REQUIRE(graph.findByName("RouterA")->getId() == 2);
    REQUIRE(graph.findByName("Missing") == nullptr);
}

TEST_CASE("Graph rejects null and duplicate nodes") {
    SECTION("rejects a null node") {
        netmind::Graph graph;
        REQUIRE_THROWS_AS(
            graph.addNode(nullptr),
            netmind::TopologyError
        );
    }

    SECTION("rejects duplicate ids") {
        netmind::Graph graph;
        graph.addNode(
            std::make_unique<netmind::Host>(1, "HostA", "10.0.0.1"));

        REQUIRE_THROWS_AS(
            graph.addNode(
                std::make_unique<netmind::Router>(1, "RouterA", "10.0.0.2")),
            netmind::TopologyError
        );
    }

    SECTION("rejects duplicate names") {
        netmind::Graph graph;
        graph.addNode(
            std::make_unique<netmind::Host>(1, "Shared", "10.0.0.1"));

        REQUIRE_THROWS_AS(
            graph.addNode(
                std::make_unique<netmind::Router>(2, "Shared", "10.0.0.2")),
            netmind::TopologyError
        );
    }
}

TEST_CASE("Graph adds links and returns neighboring links") {
    netmind::Graph graph;
    graph.addNode(
        std::make_unique<netmind::Host>(1, "HostA", "10.0.0.1"));
    graph.addNode(
        std::make_unique<netmind::Router>(2, "RouterA", "10.0.0.2"));
    graph.addNode(
        std::make_unique<netmind::Host>(3, "HostB", "10.0.0.3"));

    graph.addLink(makeLink(10, 1, 2));
    graph.addLink(makeLink(11, 2, 3));

    REQUIRE(graph.numLinks() == 2);
    REQUIRE(graph.getNeighbours(1).size() == 1);
    REQUIRE(graph.getNeighbours(2).size() == 2);
    REQUIRE(graph.getNeighbours(3).size() == 1);
    REQUIRE(graph.linkBetween(1, 2) != nullptr);
    REQUIRE(graph.linkBetween(2, 1) != nullptr);
    REQUIRE(graph.linkBetween(1, 3) == nullptr);
    REQUIRE(graph.getNeighbours(99).empty());
}

TEST_CASE("Graph rejects invalid links") {
    SECTION("rejects a null link") {
        netmind::Graph graph;
        REQUIRE_THROWS_AS(
            graph.addLink(nullptr),
            netmind::TopologyError
        );
    }

    SECTION("rejects a self-link") {
        netmind::Graph graph;
        graph.addNode(
            std::make_unique<netmind::Host>(1, "HostA", "10.0.0.1"));

        REQUIRE_THROWS_AS(
            graph.addLink(makeLink(10, 1, 1)),
            netmind::TopologyError
        );
    }

    SECTION("rejects a link with an unknown endpoint") {
        netmind::Graph graph;
        graph.addNode(
            std::make_unique<netmind::Host>(1, "HostA", "10.0.0.1"));

        REQUIRE_THROWS_AS(
            graph.addLink(makeLink(10, 1, 2)),
            netmind::TopologyError
        );
    }

    SECTION("rejects duplicate link ids") {
        netmind::Graph graph;
        graph.addNode(
            std::make_unique<netmind::Host>(1, "HostA", "10.0.0.1"));
        graph.addNode(
            std::make_unique<netmind::Router>(2, "RouterA", "10.0.0.2"));

        graph.addLink(makeLink(10, 1, 2));

        REQUIRE_THROWS_AS(
            graph.addLink(makeLink(10, 1, 2)),
            netmind::TopologyError
        );
    }

    SECTION("rejects parallel links") {
        netmind::Graph graph;
        graph.addNode(
            std::make_unique<netmind::Host>(1, "HostA", "10.0.0.1"));
        graph.addNode(
            std::make_unique<netmind::Router>(2, "RouterA", "10.0.0.2"));

        graph.addLink(makeLink(10, 1, 2));

        REQUIRE_THROWS_AS(
            graph.addLink(makeLink(11, 1, 2)),
            netmind::TopologyError
        );
    }
}

TEST_CASE("Graph returns only links whose link and endpoints are up") {
    netmind::Graph graph;
    graph.addNode(
        std::make_unique<netmind::Host>(1, "HostA", "10.0.0.1"));
    graph.addNode(
        std::make_unique<netmind::Router>(2, "RouterA", "10.0.0.2"));
    graph.addNode(
        std::make_unique<netmind::Host>(3, "HostB", "10.0.0.3"));

    graph.addLink(makeLink(10, 1, 2));
    graph.addLink(makeLink(11, 2, 3));

    REQUIRE(graph.getActiveNeighbours(2).size() == 2);

    graph.getNode(3)->setUp(false);
    REQUIRE(graph.getActiveNeighbours(2).size() == 1);

    graph.getNode(2)->setUp(false);
    REQUIRE(graph.getActiveNeighbours(2).empty());

    graph.getNode(2)->setUp(true);
    graph.getNode(3)->setUp(true);

    const auto link = graph.linkBetween(1, 2);
    REQUIRE(link != nullptr);
    const_cast<netmind::Link*>(link)->setState(netmind::LinkState::DOWN);

    REQUIRE(graph.getActiveNeighbours(2).size() == 1);
}

TEST_CASE("Graph removes links from both endpoints") {
    netmind::Graph graph;
    graph.addNode(
        std::make_unique<netmind::Host>(1, "HostA", "10.0.0.1"));
    graph.addNode(
        std::make_unique<netmind::Router>(2, "RouterA", "10.0.0.2"));

    graph.addLink(makeLink(10, 1, 2));

    REQUIRE(graph.removeLink(10));
    REQUIRE_FALSE(graph.removeLink(10));
    REQUIRE(graph.numLinks() == 0);
    REQUIRE(graph.getNeighbours(1).empty());
    REQUIRE(graph.getNeighbours(2).empty());
    REQUIRE(graph.linkBetween(1, 2) == nullptr);
}

TEST_CASE("Graph returns node ids in sorted order") {
    netmind::Graph graph;
    graph.addNode(
        std::make_unique<netmind::Host>(8, "HostC", "10.0.0.8"));
    graph.addNode(
        std::make_unique<netmind::Host>(2, "HostA", "10.0.0.2"));
    graph.addNode(
        std::make_unique<netmind::Host>(5, "HostB", "10.0.0.5"));

    REQUIRE(graph.nodeIds() == std::vector<netmind::NodeId>{2, 5, 8});
}