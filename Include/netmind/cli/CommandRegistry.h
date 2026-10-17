#pragma once

#include "netmind/cli/Command.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace netmind::cli {

struct CommandMatch {
    Command* command{};
    std::size_t wordsConsumed{};
};

class CommandRegistry {
public:
    void add(std::unique_ptr<Command> command);

    [[nodiscard]] const Command* find(const std::string& name) const noexcept;

    // Finds the longest registered command name at the start of the tokens.
    [[nodiscard]] std::optional<CommandMatch> match(
        const std::vector<std::string>& tokens) const;

    // Returns commands sorted by name for stable help output.
    [[nodiscard]] std::vector<const Command*> commands() const;

private:
    std::unordered_map<std::string, std::unique_ptr<Command>> commands_;
};

}