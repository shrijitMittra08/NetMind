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
        throw std::invalid_argument("Command name cannot start or end with whitespace");
    }
    if (commands_.count(commandName) != 0) {
        throw std::invalid_argument("Command is already registered: " + commandName);
    }
    commands_.emplace(commandName, std::move(command));
}

const Command* CommandRegistry::find(const std::string& name) const noexcept {
    const auto found = commands_.find(name);
    return found == commands_.end() ? nullptr : found->second.get();
}

std::optional<CommandMatch> CommandRegistry::match(const std::vector<std::string>& tokens) const {
    for (std::size_t wordCount = tokens.size(); wordCount > 0; --wordCount) {
        std::string candidate = tokens[0];
        for (std::size_t i = 1; i < wordCount; ++i) {
            candidate += ' ' + tokens[i];
        }
        const auto found = commands_.find(candidate);
        if (found != commands_.end()) {
            return CommandMatch{found->second.get(), wordCount};
        }
    }
    return std::nullopt;
}

std::vector<const Command*> CommandRegistry::commands() const {
    std::vector<const Command*> result;
    for (const auto& entry : commands_) {
        result.push_back(entry.second.get());
    }
    std::sort(result.begin(), result.end(), [](const Command* left, const Command* right) {
        return left->name() < right->name();
    });
    return result;
}

}
