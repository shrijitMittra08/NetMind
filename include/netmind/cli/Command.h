#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace netmind::cli {

class Session;

class Command {
  public:
    virtual ~Command() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string help() const = 0;

    virtual void execute(Session& session, const std::vector<std::string>& args,
                         std::ostream& output) = 0;
};

}