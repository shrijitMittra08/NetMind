#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "netmind/cli/CommandLineInterface.h"
#include "netmind/cli/commands/SimulationCommands.h"
#include "netmind/cli/commands/TopologyCommands.h"
#include "netmind/metrics/MetricsCollector.h"
#include "netmind/simulation/SimulationEngine.h"

namespace {

struct Options {
    std::optional<std::string> scenario;
    std::optional<std::string> script;
    std::optional<std::string> outputDirectory;
    bool quiet{false};
    bool jsonSummary{false};
};

std::string takeOptionValue(int argc, char* argv[], int& i, const std::string& option) {
    if (i + 1 >= argc) {
        throw std::invalid_argument("Missing value after " + option);
    }
    ++i;
    return argv[i];
}

Options parseOptions(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--scenario") {
            options.scenario = takeOptionValue(argc, argv, i, argument);
        } else if (argument == "--script") {
            options.script = takeOptionValue(argc, argv, i, argument);
        } else if (argument == "--out") {
            options.outputDirectory = takeOptionValue(argc, argv, i, argument);
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "--json-summary") {
            options.jsonSummary = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "Usage:\n"
                      << "  netmind_cli\n"
                      << "  netmind_cli --scenario FILE [--out DIR] [--quiet] [--json-summary]\n"
                      << "  netmind_cli --script FILE [--out DIR] [--quiet] [--json-summary]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown option: " + argument);
        }
    }
    if (options.scenario && options.script) {
        throw std::invalid_argument("--scenario and --script cannot be used together");
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
    return result + '"';
}

bool isErrorLine(const std::string& line) {
    return line.rfind("% ", 0) == 0;
}

void executeCommand(netmind::cli::CommandLineInterface& cli, const std::string& command,
                    std::ostream& output) {
    std::ostringstream commandOutput;
    cli.processLine(command, commandOutput);
    const std::string result = commandOutput.str();
    if (isErrorLine(result)) {
        throw std::runtime_error(result.substr(2));
    }
    output << result;
}

void printJsonSummary(netmind::cli::Session& session) {
    session.ensureEngine();
    if (session.metrics() == nullptr || session.engine() == nullptr) {
        throw std::runtime_error("Could not collect metrics for the JSON summary");
    }
    session.metrics()->finalize(session.engine()->now());

    const netmind::NetworkMetrics& network = session.metrics()->network();
    nlohmann::json flowSummaries = nlohmann::json::array();
    for (const netmind::FlowMetrics& flow : session.metrics()->flows()) {
        flowSummaries.push_back({{"flow_id", flow.flowId},
                                 {"sent", flow.sent},
                                 {"delivered", flow.delivered},
                                 {"dropped", flow.dropped},
                                 {"loss_pct", flow.lossPct},
                                 {"avg_latency_ms", flow.avgLatencyMs},
                                 {"throughput_mbps", flow.throughputMbps}});
    }

    const nlohmann::json summary = {{"simulation_time_s", session.engine()->now()},
                                    {"network",
                                     {{"reliability_pct", network.reliabilityPct},
                                      {"avg_throughput_mbps", network.avgThroughputMbps},
                                      {"avg_latency_ms", network.avgLatencyMs},
                                      {"total_drops", network.totalDrops},
                                      {"recovery_time_s", network.recoveryTimeS},
                                      {"events", network.events}}},
                                    {"flows", std::move(flowSummaries)}};
    std::cout << summary.dump() << '\n';
}

void runScenarioFile(netmind::cli::CommandLineInterface& cli, netmind::cli::Session& session,
                     const Options& options, std::ostream& output) {
    executeCommand(cli, "load " + quoteCommandArgument(*options.scenario), output);
    if (!session.scenarioManager().hasScenario()) {
        throw std::runtime_error("--scenario must refer to a scenario JSON file");
    }
    executeCommand(cli, "run " + std::to_string(session.scenarioManager().durationSeconds()),
                   output);
    if (options.outputDirectory) {
        executeCommand(cli, "export " + quoteCommandArgument(*options.outputDirectory), output);
    } else {
        executeCommand(cli, "export", output);
    }
}

void runScriptFile(netmind::cli::CommandLineInterface& cli, const Options& options) {
    std::ifstream script(*options.script);
    if (!script) {
        throw std::runtime_error("Could not open script file: " + *options.script);
    }
    std::ostringstream scriptOutput;
    cli.run(script, scriptOutput, false);
    if (!options.quiet) {
        std::cout << scriptOutput.str();
    }

    std::istringstream lines(scriptOutput.str());
    std::string line;
    while (std::getline(lines, line)) {
        if (isErrorLine(line)) {
            throw std::runtime_error("One or more commands in the script failed");
        }
    }
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
        std::ostream* output = &std::cout;
        if (options.quiet) {
            output = &quietOutput;
        }
        if (options.scenario) {
            runScenarioFile(cli, session, options, *output);
        } else if (options.script) {
            runScriptFile(cli, options);
        } else {
            cli.run(std::cin, *output, true);
        }

        if (options.jsonSummary) {
            printJsonSummary(session);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Project901 error: " << error.what() << '\n';
        return 1;
    }
}
