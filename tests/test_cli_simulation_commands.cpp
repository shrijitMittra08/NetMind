#include "netmind/cli/CommandLineInterface.h"
#include "netmind/cli/commands/SimulationCommands.h"
#include "netmind/cli/commands/TopologyCommands.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

class TempDirectory {
public:
    explicit TempDirectory(const std::string& label) {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("netmind_cli_" + label + "_" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

std::filesystem::path miniTopology() {
    return std::filesystem::path(NETMIND_SOURCE_DIR) /
           "tests" / "golden" / "mini_topology.json";
}

std::filesystem::path writeScenario(const std::filesystem::path& dir) {
    const auto path = dir / "cli_compare_scenario.json";
    const nlohmann::json scenario = {
        {"name", "cli_compare"},
        {"topology", miniTopology().string()},
        {"seed", 29},
        {"duration_s", 2.0},
        {"routing", {
            {"algorithm", "dijkstra"},
            {"weight", "latency"},
            {"reconvergence_delay_ms", 0}
        }},
        {"flows", nlohmann::json::array({{
            {"id", 1}, {"src", "PC1"}, {"dst", "PC2"},
            {"pattern", "constant"}, {"rate_mbps", 1.0},
            {"packet_bytes", 1000}, {"start_s", 0.0}, {"stop_s", 1.0}
        }})},
        {"events", nlohmann::json::array()}
    };
    std::ofstream output(path);
    REQUIRE(output.good());
    output << scenario.dump(2);
    output.close();
    return path;
}

void registerAll(netmind::cli::CommandLineInterface& cli) {
    netmind::cli::registerTopologyCommands(cli);
    netmind::cli::registerSimulationCommands(cli);
}

std::string quotedCommand(const std::string& command,
                          const std::filesystem::path& path) {
    return command + " \"" + path.string() + "\"";
}

void runCommand(netmind::cli::CommandLineInterface& cli,
                const std::string& command,
                std::ostringstream& output) {
    const auto previousSize = output.str().size();
    cli.processLine(command, output);
    const std::string text = output.str();
    const std::string added = text.substr(previousSize);
    INFO("Command: " << command << "\nOutput: " << added);
    CHECK(added.rfind("% ", 0) != 0);
}

}             

TEST_CASE("CLI supports routing, flow, ping, traceroute, metrics, queue, and export") {
    TempDirectory temp("core_commands");
    netmind::cli::Session session;
    session.setOutputDirectory(temp.path());
    netmind::cli::CommandLineInterface cli(session);
    registerAll(cli);
    std::ostringstream output;

    runCommand(cli, quotedCommand("load", miniTopology()), output);
    runCommand(cli, "show route R1", output);
    CHECK(output.str().find("Destination") != std::string::npos);
    runCommand(cli, "route add R1 PC2 via R2", output);
    runCommand(cli, "show route R1", output);
    CHECK(output.str().find("Static route added on R1") != std::string::npos);
    runCommand(cli, "set algo bfs", output);
    runCommand(cli, "set weight hops", output);
    runCommand(cli, "add flow PC1 PC2 1 1000 0 0.05", output);
    runCommand(cli, "run 0.05", output);
    runCommand(cli, "ping PC1 PC2 2", output);
    CHECK(output.str().find("Ping statistics: sent=2, received=2, loss=0.0%") !=
          std::string::npos);
    runCommand(cli, "traceroute PC1 PC2", output);
    CHECK(output.str().find("Tracing route from PC1 to PC2") != std::string::npos);
    runCommand(cli, "show queue PC1-R1", output);
    runCommand(cli, "show metrics", output);
    runCommand(cli, "show metrics 1", output);
    runCommand(cli, quotedCommand("export", temp.path()), output);

    CHECK(std::filesystem::exists(temp.path() / "run_001.json"));
    CHECK(std::filesystem::exists(temp.path() / "run_001_trace.csv"));
    CHECK(std::filesystem::exists(temp.path() / "run_001_flows.csv"));
    CHECK(std::filesystem::exists(temp.path() / "run_001_links.csv"));
    CHECK(std::filesystem::exists(temp.path() / "run_001_windows.csv"));
    CHECK(std::filesystem::exists(temp.path() / "run_001_network.csv"));
}

TEST_CASE("CLI schedules link, router, congestion, and burst events") {
    TempDirectory temp("event_commands");
    netmind::cli::Session session;
    session.setOutputDirectory(temp.path());
    netmind::cli::CommandLineInterface cli(session);
    registerAll(cli);
    std::ostringstream output;

    runCommand(cli, quotedCommand("load", miniTopology()), output);
    runCommand(cli, "fail link R1 R2 at 1", output);
    runCommand(cli, "restore link R1 R2 at 2", output);
    runCommand(cli, "fail router R1 at 3", output);
    runCommand(cli, "restore router R1 at 4", output);
    runCommand(cli, "congest link R1 R2 factor 0.5 at 5 for 0.5", output);
    runCommand(cli, "burst PC1 PC2 1 at 6 for 0.5", output);
    runCommand(cli, "run 7", output);

    CHECK(output.str().find("Link failure scheduled") != std::string::npos);
    CHECK(output.str().find("Link restore scheduled") != std::string::npos);
    CHECK(output.str().find("Router failure scheduled") != std::string::npos);
    CHECK(output.str().find("Router restore scheduled") != std::string::npos);
    CHECK(output.str().find("Congestion scheduled") != std::string::npos);
    CHECK(output.str().find("Burst scheduled") != std::string::npos);
    CHECK(output.str().find("Simulation time: 7.000000 s") != std::string::npos);
}

TEST_CASE("CLI step executes and reports the next scheduled event") {
    TempDirectory temp("step_command");
    netmind::cli::Session session;
    session.setOutputDirectory(temp.path());
    netmind::cli::CommandLineInterface cli(session);
    registerAll(cli);
    std::ostringstream output;

    runCommand(cli, quotedCommand("load", miniTopology()), output);
    runCommand(cli, "fail link R1 R2 at 1", output);
    runCommand(cli, "step", output);
    CHECK(output.str().find("LINK_DOWN") != std::string::npos);
}

TEST_CASE("CLI changes the seed, resets, and retains the loaded topology") {
    TempDirectory temp("reset_command");
    netmind::cli::Session session;
    session.setOutputDirectory(temp.path());
    netmind::cli::CommandLineInterface cli(session);
    registerAll(cli);
    std::ostringstream output;

    runCommand(cli, quotedCommand("load", miniTopology()), output);
    runCommand(cli, "set seed 8675309", output);
    CHECK(session.scenarioManager().seed() == 8675309);
    runCommand(cli, "show topology", output);
    CHECK(output.str().find("Topology: 4 nodes, 3 links") != std::string::npos);
    runCommand(cli, "reset", output);
}

TEST_CASE("CLI compares algorithms using the same loaded scenario") {
    TempDirectory temp("compare_command");
    const auto scenario = writeScenario(temp.path());
    netmind::cli::Session session;
    session.setOutputDirectory(temp.path());
    netmind::cli::CommandLineInterface cli(session);
    registerAll(cli);
    std::ostringstream output;

    runCommand(cli, quotedCommand("load", scenario), output);
    runCommand(cli, "compare PC1 PC2", output);
    const std::string text = output.str();

    CHECK(text.find("Recovery(s)") != std::string::npos);
    CHECK(text.find("Route computes") != std::string::npos);
    CHECK(text.find("Compute us") != std::string::npos);
    CHECK(text.find("bfs") != std::string::npos);
    CHECK(text.find("dijkstra") != std::string::npos);
    CHECK(text.find("bellman-ford") != std::string::npos);
}
