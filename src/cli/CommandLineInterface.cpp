#include "netmind/cli/CommandLineInterface.h"

#include <stdexcept>
#include <utility>

namespace netmind::cli {
namespace {

class HelpCommand final : public Command {
public:
    explicit HelpCommand(const CommandRegistry& registry)
        : registry_(registry) {}

    [[nodiscard]] std::string name() const override {
        return "help";
    }

    [[nodiscard]] std::string help() const override {
        return "List available commands";
    }

    void execute(
        Session&,
        const std::vector<std::string>& args,
        std::ostream& output) override {
        if (!args.empty()) {
            throw std::invalid_argument("Usage: help");
        }

        output << "Available commands:\n";
        for (const Command* command : registry_.commands()) {
            output << "  " << command->name()
                   << " - " << command->help() << '\n';
        }
    }

private:
    const CommandRegistry& registry_;
};

class ExitCommand final : public Command {
public:
    [[nodiscard]] std::string name() const override {
        return "exit";
    }

    [[nodiscard]] std::string help() const override {
        return "Exit Project901";
    }

    void execute(
        Session& session,
        const std::vector<std::string>& args,
        std::ostream&) override {
        if (!args.empty()) {
            throw std::invalid_argument("Usage: exit");
        }

        session.requestExit();
    }
};

}             

CommandLineInterface::CommandLineInterface(Session& session)
    : session_(session) {
    registry_.add(std::make_unique<HelpCommand>(registry_));
    registry_.add(std::make_unique<ExitCommand>());
}

void CommandLineInterface::addCommand(std::unique_ptr<Command> command) {
    registry_.add(std::move(command));
}

void CommandLineInterface::run(
    std::istream& input,
    std::ostream& output,
    bool showPrompt) {
    std::string line;

    while (!session_.exitRequested()) {
        if (showPrompt) {
            output << "netmind> " << std::flush;
        }

        if (!std::getline(input, line)) {
            break;
        }

        processLine(line, output);
    }
}

void CommandLineInterface::processLine(
    const std::string& line,
    std::ostream& output) {
    std::vector<std::string> tokens;

    try {
        tokens = tokenize(line);
    } catch (const std::exception& error) {
        output << "% " << error.what() << '\n';
        return;
    }

    if (tokens.empty()) {
        return;
    }

    const auto commandMatch = registry_.match(tokens);
    if (!commandMatch.has_value()) {
        output << "% Unknown command: " << tokens.front() << '\n';
        return;
    }

    std::vector<std::string> args(
        tokens.begin() + commandMatch->wordsConsumed,
        tokens.end());

    try {
        commandMatch->command->execute(session_, args, output);
    } catch (const std::exception& error) {
        output << "% " << error.what() << '\n';
    }
}

std::vector<std::string> CommandLineInterface::tokenize(
    const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool tokenStarted = false;
    bool inQuotes = false;
    char quoteCharacter = '\0';

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char character = line[i];

        if (inQuotes) {
            if (character == quoteCharacter) {
                inQuotes = false;
                continue;
            }

            if (character == '\\') {
                const bool hasNext = i + 1 < line.size();
                if (hasNext) {
                    const char next = line[i + 1];

                                                                      
                                                                     
                    if (next == quoteCharacter || next == '\\') {
                        current += next;
                        ++i;
                        tokenStarted = true;
                        continue;
                    }
                }

                                                                     
                                                  
                current += character;
                tokenStarted = true;
                continue;
            }

            current += character;
            tokenStarted = true;
            continue;
        }

        if (character == '"' || character == '\'') {
            inQuotes = true;
            quoteCharacter = character;
            tokenStarted = true;
            continue;
        }

        if (character == ' ' || character == '\t' ||
            character == '\r' || character == '\n') {
            if (tokenStarted) {
                tokens.push_back(current);
                current.clear();
                tokenStarted = false;
            }
            continue;
        }

        current += character;
        tokenStarted = true;
    }

    if (inQuotes) {
        throw std::invalid_argument("Unclosed quoted argument");
    }

    if (tokenStarted) {
        tokens.push_back(current);
    }

    return tokens;
}

const CommandRegistry& CommandLineInterface::registry() const noexcept {
    return registry_;
}

}