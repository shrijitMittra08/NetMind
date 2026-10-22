#include "netmind/SimObserver.h"
#include "netmind/graph/Graph.h"
#include "netmind/model/Flow.h"
#include "netmind/model/Host.h"
#include "netmind/model/Link.h"
#include "netmind/model/TrafficPattern.h"
#include "netmind/simulation/SimulationEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

class FlowRecorder final : public netmind::SimObserver {
public:
    void onSent(
        netmind::SimTime time,
        const netmind::Packet& packet
    ) override {
        sentTimes.push_back(time);
        flowIds.push_back(packet.flowId);
    }

    void onDelivered(
        netmind::SimTime,
        const netmind::Packet& packet
    ) override {
        deliveredFlowIds.push_back(packet.flowId);
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
    std::vector<netmind::FlowId> deliveredFlowIds;
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

TEST_CASE("SimulationEngine generates a flow until its stop time") {
    netmind::SimulationEngine engine(1234);
    FlowRecorder recorder;

    engine.addObserver(recorder);
    engine.setGraph(makeTwoHostGraph());

    engine.addFlow(netmind::Flow(
        7,
        1,
        2,
        0.0,
        0.0025,
        std::make_unique<netmind::ConstantRate>(8.0, 1000)));

    engine.run();

    REQUIRE(recorder.sentTimes.size() == 3);
    REQUIRE(recorder.sentTimes[0] == 0.0);
    REQUIRE(recorder.sentTimes[1] == 0.001);
    REQUIRE(recorder.sentTimes[2] == 0.002);

    REQUIRE(recorder.flowIds.size() == 3);
    REQUIRE(recorder.flowIds[0] == 7);
    REQUIRE(recorder.flowIds[1] == 7);
    REQUIRE(recorder.flowIds[2] == 7);

    REQUIRE(recorder.deliveredFlowIds.size() == 3);
    REQUIRE(recorder.flowEvents.size() == 2);
    REQUIRE(recorder.flowEvents[0] == "FLOW_START");
    REQUIRE(recorder.flowEvents[1] == "FLOW_STOP");
}