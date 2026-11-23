#include "netmind/SimObserver.h"
#include "netmind/common/Errors.h"
#include "netmind/model/Packet.h"
#include "netmind/simulation/ScenarioManager.h"
#include "netmind/simulation/SimulationEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

class ScenarioRecorder final : public netmind::SimObserver {
public:
    void onSent(
        netmind::SimTime time,
        const netmind::Packet& packet) override {
        sentTimes.push_back(time);
        flowIds.push_back(packet.flowId);
    }

    std::vector<netmind::SimTime> sentTimes;
    std::vector<netmind::FlowId> flowIds;
};

std::filesystem::path writeScenario(
    const nlohmann::json& document,
    const std::string& fileName) {
    const auto path =
        std::filesystem::temp_directory_path() / fileName;

    std::ofstream output(path);
    REQUIRE(output.good());

    output << document.dump(2);
    output.close();

    return path;
}

nlohmann::json validScenario() {
    const std::filesystem::path topology =
        std::filesystem::path(NETMIND_SOURCE_DIR) /
        "tests" / "golden" / "mini_topology.json";

    return {
        {"name", "scenario_manager_test"},
        {"topology", topology.string()},
        {"seed", 12345},
        {"duration_s", 0.003},
        {"routing", {
            {"algorithm", "dijkstra"},
            {"weight", "latency"},
            {"reconvergence_delay_ms", 0}
        }},
        {"flows", nlohmann::json::array({
            {
                {"id", 7},
                {"src", "PC1"},
                {"dst", "PC2"},
                {"pattern", "constant"},
                {"rate_mbps", 8.0},
                {"packet_bytes", 1000},
                {"start_s", 0.0},
                {"stop_s", 0.003}
            }
        })},
        {"events", nlohmann::json::array()}
    };
}

}             

// Checks that a scenario file loads the topology and seed and sends the expected flow packets
TEST_CASE("ScenarioManager loads and runs a configured scenario") {
    const auto scenarioPath =
        writeScenario(
            validScenario(),
            "netmind_scenario_manager_test.json");

    netmind::ScenarioManager manager;
    manager.loadScenario(scenarioPath.string());

    REQUIRE(manager.graph().numNodes() == 4);
    REQUIRE(manager.graph().numLinks() == 3);
    REQUIRE(manager.seed() == 12345);

    netmind::SimulationEngine engine(1);
    ScenarioRecorder recorder;
    engine.addObserver(recorder);

    manager.run(engine, 0.003);

    REQUIRE(recorder.sentTimes.size() == 3);
    REQUIRE(recorder.flowIds.size() == 3);
    REQUIRE(recorder.flowIds[0] == 7);
    REQUIRE(recorder.flowIds[1] == 7);
    REQUIRE(recorder.flowIds[2] == 7);

    std::filesystem::remove(scenarioPath);
}

// Checks that a flow naming a host that does not exist makes scenario loading fail
TEST_CASE("ScenarioManager rejects flow references to unknown hosts") {
    auto document = validScenario();
    document["flows"][0]["src"] = "MissingHost";

    const auto scenarioPath =
        writeScenario(
            document,
            "netmind_scenario_manager_invalid_test.json");

    netmind::ScenarioManager manager;

    REQUIRE_THROWS_AS(
        manager.loadScenario(scenarioPath.string()),
        netmind::ConfigError);

    std::filesystem::remove(scenarioPath);
}

// Checks that running longer than the scenario duration throws a config error
TEST_CASE("ScenarioManager rejects a run beyond the scenario duration") {
    const auto scenarioPath =
        writeScenario(
            validScenario(),
            "netmind_scenario_manager_duration_test.json");

    netmind::ScenarioManager manager;
    manager.loadScenario(scenarioPath.string());

    netmind::SimulationEngine engine(1);

    REQUIRE_THROWS_AS(
        manager.run(engine, 1.0),
        netmind::ConfigError);

    std::filesystem::remove(scenarioPath);
}

// Checks that a cloned scenario keeps the same seed and topology but uses the new algorithm
TEST_CASE("ScenarioManager cloneWith preserves the scenario and seed") {
    const auto scenarioPath =
        writeScenario(
            validScenario(),
            "netmind_scenario_manager_clone_test.json");

    netmind::ScenarioManager original;
    original.loadScenario(scenarioPath.string());

    auto clone = original.cloneWith("bfs");

    REQUIRE(clone.seed() == original.seed());
    REQUIRE(clone.graph().numNodes() == original.graph().numNodes());
    REQUIRE(clone.graph().numLinks() == original.graph().numLinks());

    netmind::SimulationEngine engine(1);
    clone.run(engine, 0.003);

    REQUIRE(engine.routingManager().algorithmName() == "bfs");

    std::filesystem::remove(scenarioPath);
}

// Checks that cloning a scenario with an unknown algorithm name throws a config error
TEST_CASE("ScenarioManager cloneWith rejects an unknown algorithm") {
    const auto scenarioPath =
        writeScenario(
            validScenario(),
            "netmind_scenario_manager_bad_clone_test.json");

    netmind::ScenarioManager manager;
    manager.loadScenario(scenarioPath.string());

    REQUIRE_THROWS_AS(
        manager.cloneWith("not-a-routing-algorithm"),
        netmind::ConfigError);

    std::filesystem::remove(scenarioPath);
}