#include "netmind/cli/CommandLineInterface.h"
#include "netmind/cli/commands/TopologyCommands.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace {

std::filesystem::path miniTopologyPath() {
    return std::filesystem::path(NETMIND_SOURCE_DIR) /
           "tests" / "golden" / "mini_topology.json";
}

std::filesystem::path writeScenarioFile() {
    const auto path =
        std::filesystem::temp_directory_path() /
        "netmind_cli_topology_command_scenario.json";

    const nlohmann::json document = {
        {"name", "cli_test"},
        {"topology", miniTopologyPath().string()},
        {"seed", 123},
        {"duration_s", 0.01},
        {"routing", {
            {"algorithm", "dijkstra"},
            {"weight", "latency"},
            {"reconvergence_delay_ms", 0}
        }},
        {"flows", nlohmann::json::array()},
        {"events", nlohmann::json::array()}
    };

    std::ofstream output(path);
    REQUIRE(output.good());
    output << document.dump(2);
    output.close();

    return path;
}

} // namespace

TEST_CASE("CLI loads a topology and displays its nodes and links") {
    netmind::cli::Session session;
    netmind::cli::CommandLineInterface cli(session);
    netmind::cli::registerTopologyCommands(cli);

    std::ostringstream output;
    const auto path = miniTopologyPath();

    cli.processLine("load \"" + path.string() + "\"", output);
    cli.processLine("show topology", output);

    const std::string text = output.str();
    CHECK(text.find("Loaded topology: 4 nodes, 3 links") != std::string::npos);
    CHECK(text.find("Topology: 4 nodes, 3 links") != std::string::npos);
    CHECK(text.find("PC1") != std::string::npos);
    CHECK(text.find("R1") != std::string::npos);
    CHECK(text.find("PC1 <-> R1") != std::string::npos);
    CHECK(text.find("100.00") != std::string::npos);
}

TEST_CASE("CLI load accepts a scenario file") {
    netmind::cli::Session session;
    netmind::cli::CommandLineInterface cli(session);
    netmind::cli::registerTopologyCommands(cli);

    const auto path = writeScenarioFile();
    std::ostringstream output;

    cli.processLine("load \"" + path.string() + "\"", output);

    CHECK(output.str() == "Loaded scenario: 4 nodes, 3 links\n");
    CHECK(session.scenarioManager().graph().numNodes() == 4);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("CLI show topology reports when no topology is loaded") {
    netmind::cli::Session session;
    netmind::cli::CommandLineInterface cli(session);
    netmind::cli::registerTopologyCommands(cli);

    std::ostringstream output;
    cli.processLine("show topology", output);

    CHECK(output.str().find("% No topology has been loaded") == 0);
}