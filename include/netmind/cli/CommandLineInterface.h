#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "netmind/cli/CommandRegistry.h"
#include "netmind/cli/Session.h"

namespace netmind::cli {

class CommandLineInterface {
  public:
    explicit CommandLineInterface(Session& session);

    void addCommand(std::unique_ptr<Command> command);

    void run(std::istream& input, std::ostream& output, bool showPrompt = false);

    void processLine(const std::string& line, std::ostream& output);

    [[nodiscard]] static std::vector<std::string> tokenize(const std::string& line);

    [[nodiscard]] const CommandRegistry& registry() const noexcept;

  private:
    Session& session_;
    CommandRegistry registry_;
};

}