#include "netmind/cli/CommandRegistry.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace netmind::cli {

void CommandRegistry::add(std::unique_ptr<Command> command) {
    if (command == nullptr) {
        throw std::invalid_argument("Cannot register a null command");
    }

    const std::string commandName = command->name();
    if (commandName.empty()) {
        throw std::invalid_argument("Command name cannot be empty");
    }

    if (commandName.front() == ' ' || commandName.back() == ' ') {
        throw std::invalid_argument(
            "Command name cannot start or end with whitespace");
    }

    if (commands_.find(commandName) != commands_.end()) {
        throw std::invalid_argument(
            "Command is already registered: " + commandName);
    }

    commands_.emplace(commandName, std::move(command));
}

const Command* CommandRegistry::find(const std::string& name) const noexcept {
    const auto it = commands_.find(name);
    return it == commands_.end() ? nullptr : it->second.get();
}

std::optional<CommandMatch> CommandRegistry::match(
    const std::vector<std::string>& tokens) const {
    if (tokens.empty()) {
        return std::nullopt;
    }

    for (std::size_t wordCount = tokens.size(); wordCount > 0; --wordCount) {
        std::string candidate;

        for (std::size_t i = 0; i < wordCount; ++i) {
            if (i != 0) {
                candidate += ' ';
            }
            candidate += tokens[i];
        }

        const auto it = commands_.find(candidate);
        if (it != commands_.end()) {
            return CommandMatch{it->second.get(), wordCount};
        }
    }

    return std::nullopt;
}

std::vector<const Command*> CommandRegistry::commands() const {
    std::vector<const Command*> result;
    result.reserve(commands_.size());

    for (const auto& entry : commands_) {
        result.push_back(entry.second.get());
    }

    std::sort(
        result.begin(),
        result.end(),
        [](const Command* left, const Command* right) {
            return left->name() < right->name();
        });

    return result;
}

}