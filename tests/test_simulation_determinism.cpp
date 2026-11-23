#include "netmind/metrics/TraceLogger.h"
#include "netmind/simulation/ScenarioManager.h"
#include "netmind/simulation/SimulationEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

namespace {

std::filesystem::path writeScenario() {
    const std::filesystem::path topology =
        std::filesystem::path(NETMIND_SOURCE_DIR) /
        "tests" / "golden" / "mini_topology.json";

    const nlohmann::json document = {
        {"name", "determinism_test"},
        {"topology", topology.string()},
        {"seed", 12345},
        {"duration_s", 0.003},
        {"routing", {
            {"algorithm", "dijkstra"},
            {"weight", "latency"}
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

    const auto path =
        std::filesystem::temp_directory_path() /
        "netmind_determinism_scenario.json";

    std::ofstream output(path);
    REQUIRE(output.good());
    output << document.dump(2);
    output.close();

    return path;
}

void runAndWriteTrace(
    netmind::ScenarioManager& manager,
    const std::filesystem::path& tracePath) {
    {
        netmind::TraceLogger logger(tracePath.string());
        netmind::SimulationEngine engine(1);
        engine.addObserver(logger);
        manager.run(engine, 0.003);
    }
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    REQUIRE(input.good());

    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

}             

// Checks that running the same scenario with the same seed twice writes identical trace files
TEST_CASE("Same scenario and seed produce identical trace files") {
    const auto scenarioPath = writeScenario();

    const auto firstTracePath =
        std::filesystem::temp_directory_path() /
        "netmind_determinism_first.csv";
    const auto secondTracePath =
        std::filesystem::temp_directory_path() /
        "netmind_determinism_second.csv";

    netmind::ScenarioManager firstRun;
    firstRun.loadScenario(scenarioPath.string());

                                                                           
                                                   
    auto secondRun = firstRun.cloneWith("dijkstra");

    runAndWriteTrace(firstRun, firstTracePath);
    runAndWriteTrace(secondRun, secondTracePath);

    const std::string firstTrace = readFile(firstTracePath);
    const std::string secondTrace = readFile(secondTracePath);

    REQUIRE_FALSE(firstTrace.empty());
    REQUIRE(firstTrace == secondTrace);

    std::filesystem::remove(scenarioPath);
    std::filesystem::remove(firstTracePath);
    std::filesystem::remove(secondTracePath);
}