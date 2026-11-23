#include "netmind/SimObserver.h"
#include "netmind/common/Errors.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/simulation/SimulationEngine.h"
#include "netmind/simulation/events/CongestionEvent.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

class NetworkEventRecorder final : public netmind::SimObserver {
public:
    void onNetworkEvent(
        netmind::SimTime time,
        const std::string& type,
        const std::string&,
        const std::string&
    ) override {
        times.push_back(time);
        types.push_back(type);
    }

    std::vector<netmind::SimTime> times;
    std::vector<std::string> types;
};

std::unique_ptr<netmind::Graph> makeLinkGraph() {
    auto graph = std::make_unique<netmind::Graph>();

    graph->addNode(std::make_unique<netmind::Host>(
        1, "HostA", "10.0.0.1"));
    graph->addNode(std::make_unique<netmind::Host>(
        2, "HostB", "10.0.0.2"));

    graph->addLink(std::make_unique<netmind::Link>(
        10, 1, 2, 100.0, 2.0, 0.01, 8));

    return graph;
}

}             

// Checks that congestion cuts a link's bandwidth and adds loss, then undoes it when time is up
TEST_CASE("Congestion temporarily degrades and then restores a link") {
    netmind::SimulationEngine engine(1234);
    NetworkEventRecorder recorder;

    engine.addObserver(recorder);
    engine.setGraph(makeLinkGraph());

    engine.scheduleEvent(
        1.0,
        std::make_shared<netmind::CongestionEvent>(
            10, 0.25, 0.10, 1.5));

    engine.runUntil(1.0);

    const netmind::Link* link = engine.graph().getLink(10);
    REQUIRE(link != nullptr);
    REQUIRE(link->state() == netmind::LinkState::DEGRADED);
    REQUIRE(link->effectiveBandwidth() == 25.0);
    REQUIRE(link->effectiveLossRate() == 0.11);

    engine.runUntil(2.5);

    REQUIRE(link->state() == netmind::LinkState::UP);
    REQUIRE(link->effectiveBandwidth() == 100.0);
    REQUIRE(link->effectiveLossRate() == 0.01);

    REQUIRE(recorder.types.size() == 2);
    REQUIRE(recorder.types[0] == "LINK_DEGRADED");
    REQUIRE(recorder.types[1] == "LINK_UP");
    REQUIRE(recorder.times[0] == 1.0);
    REQUIRE(recorder.times[1] == 2.5);
}

// Checks that a congestion event throws an error for a zero factor, negative loss or zero duration
TEST_CASE("Congestion event rejects invalid parameters") {
    REQUIRE_THROWS_AS(
        netmind::CongestionEvent(10, 0.0, 0.0, 1.0),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::CongestionEvent(10, 0.5, -0.1, 1.0),
        netmind::ConfigError);

    REQUIRE_THROWS_AS(
        netmind::CongestionEvent(10, 0.5, 0.0, 0.0),
        netmind::ConfigError);
}