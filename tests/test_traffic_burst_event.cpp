#include "netmind/SimObserver.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/simulation/SimulationEngine.h"
#include "netmind/simulation/events/TrafficBurstEvent.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

class BurstRecorder final : public netmind::SimObserver {
public:
    void onSent(
        netmind::SimTime time,
        const netmind::Packet& packet
    ) override {
        sentTimes.push_back(time);
        flowIds.push_back(packet.flowId);
    }

    void onNetworkEvent(
        netmind::SimTime,
        const std::string& type,
        const std::string&,
        const std::string&
    ) override {
        flowEvents.push_back(type);
    }

    std::vector<netmind::SimTime> sentTimes;
    std::vector<netmind::FlowId> flowIds;
    std::vector<std::string> flowEvents;
};

std::unique_ptr<netmind::Graph> makeTwoHostGraph() {
    auto graph = std::make_unique<netmind::Graph>();

    auto hostA = std::make_unique<netmind::Host>(
        1, "HostA", "10.0.0.1");
    hostA->setGatewayId(2);

    graph->addNode(std::move(hostA));
    graph->addNode(std::make_unique<netmind::Host>(
        2, "HostB", "10.0.0.2"));

    graph->addLink(std::make_unique<netmind::Link>(
        10, 1, 2, 100.0, 0.0, 0.0, 8));

    return graph;
}

}             

TEST_CASE("Traffic burst creates a temporary flow") {
    netmind::SimulationEngine engine(1234);
    BurstRecorder recorder;

    engine.addObserver(recorder);
    engine.setGraph(makeTwoHostGraph());

    engine.scheduleEvent(
        1.0,
        std::make_shared<netmind::TrafficBurstEvent>(
            9, 1, 2, 0.003, 8.0, 1000));

    engine.run();

    REQUIRE(recorder.sentTimes.size() == 3);
    REQUIRE(std::abs(recorder.sentTimes[0] - 1.0) < 1e-12);
    REQUIRE(std::abs(recorder.sentTimes[1] - 1.001) < 1e-12);
    REQUIRE(std::abs(recorder.sentTimes[2] - 1.002) < 1e-12);

    REQUIRE(recorder.flowIds.size() == 3);
    REQUIRE(recorder.flowIds[0] == 9);
    REQUIRE(recorder.flowIds[1] == 9);
    REQUIRE(recorder.flowIds[2] == 9);

    REQUIRE(recorder.flowEvents.size() == 2);
    REQUIRE(recorder.flowEvents[0] == "FLOW_START");
    REQUIRE(recorder.flowEvents[1] == "FLOW_STOP");
}

TEST_CASE("Traffic burst rejects invalid configuration") {
    REQUIRE_THROWS(
        netmind::TrafficBurstEvent(9, 1, 2, 0.0, 8.0, 1000));

    REQUIRE_THROWS(
        netmind::TrafficBurstEvent(9, 1, 2, 1.0, 0.0, 1000));

    REQUIRE_THROWS(
        netmind::TrafficBurstEvent(9, 1, 2, 1.0, 8.0, 0));
}