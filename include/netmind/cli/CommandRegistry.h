#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "netmind/cli/Command.h"

namespace netmind::cli {

struct CommandMatch {
    Command* command{};
    std::size_t wordsConsumed{};
};

class CommandRegistry {
  public:
    void add(std::unique_ptr<Command> command);

    [[nodiscard]] const Command* find(const std::string& name) const noexcept;

    [[nodiscard]] std::optional<CommandMatch> match(const std::vector<std::string>& tokens) const;

    [[nodiscard]] std::vector<const Command*> commands() const;

  private:
    std::unordered_map<std::string, std::unique_ptr<Command>> commands_;
};

}