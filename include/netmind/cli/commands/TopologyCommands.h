#pragma once

#include "netmind/cli/Command.h"

namespace netmind::cli {

class CommandLineInterface;

class LoadCommand final : public Command {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string help() const override;

    void execute(Session& session, const std::vector<std::string>& args,
                 std::ostream& output) override;
};

class ShowTopologyCommand final : public Command {
  public:
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] std::string help() const override;

    void execute(Session& session, const std::vector<std::string>& args,
                 std::ostream& output) override;
};

void registerTopologyCommands(CommandLineInterface& cli);

}