#include "netmind/cli/CommandLineInterface.h"
#include "netmind/cli/commands/SimulationCommands.h"
#include "netmind/cli/commands/TopologyCommands.h"
#include "netmind/metrics/MetricsCollector.h"
#include "netmind/simulation/SimulationEngine.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

struct Options {
    std::optional<std::string> scenario;
    std::optional<std::string> script;
    std::optional<std::string> outputDirectory;
    bool quiet{false};
    bool jsonSummary{false};
};

Options parseOptions(int argc, char* argv[]) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];

        const auto requireValue = [&](const std::string& option) {
            if (i + 1 >= argc) {
                throw std::invalid_argument("Missing value after " + option);
            }
            return std::string(argv[++i]);
        };

        if (argument == "--scenario") {
            options.scenario = requireValue(argument);
        } else if (argument == "--script") {
            options.script = requireValue(argument);
        } else if (argument == "--out") {
            options.outputDirectory = requireValue(argument);
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "--json-summary") {
            options.jsonSummary = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout
                << "Usage:\n"
                << "  netmind_cli\n"
                << "  netmind_cli --scenario FILE [--out DIR] "
                   "[--quiet] [--json-summary]\n"
                << "  netmind_cli --script FILE [--out DIR] "
                   "[--quiet] [--json-summary]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown option: " + argument);
        }
    }

    if (options.scenario && options.script) {
        throw std::invalid_argument(
            "--scenario and --script cannot be used together");
    }

    return options;
}

std::string quoteCommandArgument(const std::string& value) {
    std::string result{"\""};

    for (const char character : value) {
        if (character == '\\' || character == '"') {
            result += '\\';
        }
        result += character;
    }

    result += '"';
    return result;
}

void executeCommand(
    netmind::cli::CommandLineInterface& cli,
    const std::string& command,
    std::ostream& output) {
    std::ostringstream commandOutput;
    cli.processLine(command, commandOutput);

    const std::string result = commandOutput.str();
    if (result.rfind("% ", 0) == 0) {
        throw std::runtime_error(result.substr(2));
    }

    output << result;
}

bool containsCommandError(const std::string& text) {
    std::istringstream lines(text);
    std::string line;

    while (std::getline(lines, line)) {
        if (line.rfind("% ", 0) == 0) {
            return true;
        }
    }

    return false;
}

void printJsonSummary(netmind::cli::Session& session) {
    session.ensureEngine();

    if (session.metrics() == nullptr || session.engine() == nullptr) {
        throw std::runtime_error(
            "Could not collect metrics for the JSON summary");
    }

    session.metrics()->finalize(session.engine()->now());

    const auto& network = session.metrics()->network();
    nlohmann::json flowSummaries = nlohmann::json::array();

    for (const auto& flow : session.metrics()->flows()) {
        flowSummaries.push_back({
            {"flow_id", flow.flowId},
            {"sent", flow.sent},
            {"delivered", flow.delivered},
            {"dropped", flow.dropped},
            {"loss_pct", flow.lossPct},
            {"avg_latency_ms", flow.avgLatencyMs},
            {"throughput_mbps", flow.throughputMbps}
        });
    }

    const nlohmann::json summary = {
        {"simulation_time_s", session.engine()->now()},
        {"network", {
            {"reliability_pct", network.reliabilityPct},
            {"avg_throughput_mbps", network.avgThroughputMbps},
            {"avg_latency_ms", network.avgLatencyMs},
            {"total_drops", network.totalDrops},
            {"recovery_time_s", network.recoveryTimeS},
            {"events", network.events}
        }},
        {"flows", std::move(flowSummaries)}
    };

    std::cout << summary.dump() << '\n';
}

}

int main(int argc, char* argv[]) {
    try {
        const Options options = parseOptions(argc, argv);

        netmind::cli::Session session;
        if (options.outputDirectory) {
            session.setOutputDirectory(*options.outputDirectory);
        }

        netmind::cli::CommandLineInterface cli(session);
        netmind::cli::registerTopologyCommands(cli);
        netmind::cli::registerSimulationCommands(cli);

        std::ostringstream quietOutput;
        std::ostream& output =
            options.quiet ? static_cast<std::ostream&>(quietOutput)
                          : static_cast<std::ostream&>(std::cout);

        if (options.scenario) {
            executeCommand(
                cli,
                "load " + quoteCommandArgument(*options.scenario),
                output);

            if (!session.scenarioManager().hasScenario()) {
                throw std::runtime_error(
                    "--scenario must refer to a scenario JSON file");
            }

            executeCommand(
                cli,
                "run " + std::to_string(
                    session.scenarioManager().durationSeconds()),
                output);

            if (options.outputDirectory) {
                executeCommand(
                    cli,
                    "export " +
                        quoteCommandArgument(*options.outputDirectory),
                    output);
            } else {
                executeCommand(cli, "export", output);
            }
        } else if (options.script) {
            std::ifstream script(*options.script);
            if (!script) {
                throw std::runtime_error(
                    "Could not open script file: " + *options.script);
            }

            std::ostringstream scriptOutput;
            cli.run(script, scriptOutput, false);

            const std::string result = scriptOutput.str();
            if (!options.quiet) {
                std::cout << result;
            }

            if (containsCommandError(result)) {
                throw std::runtime_error(
                    "One or more commands in the script failed");
            }
        } else {
            cli.run(std::cin, output, true);
        }

        if (options.jsonSummary) {
            printJsonSummary(session);
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "NetMind error: " << error.what() << '\n';
        return 1;
    }
}