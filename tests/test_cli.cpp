#include "netmind/cli/CommandLineInterface.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class EchoCommand final : public netmind::cli::Command {
public:
    [[nodiscard]] std::string name() const override {
        return "show route";
    }

    [[nodiscard]] std::string help() const override {
        return "Show a router's forwarding table";
    }

    void execute(
        netmind::cli::Session&,
        const std::vector<std::string>& args,
        std::ostream& output) override {
        output << "args:";
        for (const auto& arg : args) {
            output << " [" << arg << "]";
        }
        output << '\n';
    }
};

}             

TEST_CASE("CLI tokenizer keeps quoted names together") {
    const auto tokens =
        netmind::cli::CommandLineInterface::tokenize(
            "show route \"Router One\"");

    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0] == "show");
    CHECK(tokens[1] == "route");
    CHECK(tokens[2] == "Router One");
}

TEST_CASE("CLI tokenizer preserves Windows paths inside quotes") {
    const auto tokens =
        netmind::cli::CommandLineInterface::tokenize(
            R"(load "C:\Users\shrij\Downloads\netmind.json")");

    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0] == "load");
    CHECK(tokens[1] == R"(C:\Users\shrij\Downloads\netmind.json)");
}

TEST_CASE("CLI tokenizer reports an unclosed quoted argument") {
    CHECK_THROWS_AS(
        netmind::cli::CommandLineInterface::tokenize("load \"topology.json"),
        std::invalid_argument);
}

TEST_CASE("CLI dispatches the longest matching multiword command") {
    netmind::cli::Session session;
    netmind::cli::CommandLineInterface cli(session);
    cli.addCommand(std::make_unique<EchoCommand>());

    std::ostringstream output;
    cli.processLine("show route \"Router One\"", output);

    CHECK(output.str() == "args: [Router One]\n");
}

TEST_CASE("CLI help lists registered commands in sorted order") {
    netmind::cli::Session session;
    netmind::cli::CommandLineInterface cli(session);

    std::ostringstream output;
    cli.processLine("help", output);

    const std::string text = output.str();
    const auto exitPosition = text.find("  exit - ");
    const auto helpPosition = text.find("  help - ");

    REQUIRE(exitPosition != std::string::npos);
    REQUIRE(helpPosition != std::string::npos);
    CHECK(exitPosition < helpPosition);
}

TEST_CASE("CLI reports unknown commands and continues reading") {
    netmind::cli::Session session;
    netmind::cli::CommandLineInterface cli(session);

    std::istringstream input("nonsense\nhelp\nexit\n");
    std::ostringstream output;

    cli.run(input, output);

    CHECK(output.str().find("% Unknown command: nonsense\n") == 0);
    CHECK(output.str().find("Available commands:") != std::string::npos);
    CHECK(session.exitRequested());
}

TEST_CASE("CLI stops after the exit command") {
    netmind::cli::Session session;
    netmind::cli::CommandLineInterface cli(session);

    std::istringstream input("exit\nhelp\n");
    std::ostringstream output;

    cli.run(input, output);

    CHECK(session.exitRequested());
    CHECK(output.str().empty());
}