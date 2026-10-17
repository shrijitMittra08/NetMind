#include "netmind/common/Errors.h"
#include "netmind/model/Node.h"

#include <utility>
#include <catch2/catch_test_macros.hpp>

namespace {

class TestNode final : public netmind::Node {
public:
    TestNode(netmind::NodeId id, std::string name, std::string ip)
        : Node(id, std::move(name), std::move(ip), netmind::NodeType::HOST) {}

    void receivePacket(
        netmind::Packet,
        netmind::LinkId,
        netmind::SimContext&) override {}
};

}

TEST_CASE("Node stores its common properties and starts up") {
    TestNode node(7, "PC1", "10.0.0.1");

    REQUIRE(node.getId() == 7);
    REQUIRE(node.name() == "PC1");
    REQUIRE(node.ip() == "10.0.0.1");
    REQUIRE(node.type() == netmind::NodeType::HOST);
    REQUIRE(node.isUp());

    node.setUp(false);
    REQUIRE_FALSE(node.isUp());
}

TEST_CASE("Node rejects an empty name") {
    REQUIRE_THROWS_AS(
        TestNode(1, "", "10.0.0.1"),
        netmind::ConfigError);
}

TEST_CASE("Node rejects an invalid IPv4 address") {
    REQUIRE_THROWS_AS(
        TestNode(1, "PC1", "256.0.0.1"),
        netmind::ConfigError);
}