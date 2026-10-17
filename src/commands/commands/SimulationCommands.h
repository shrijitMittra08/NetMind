#pragma once

namespace netmind::cli {

class CommandLineInterface;

// Registers the operational commands described in section 5B.2.
void registerSimulationCommands(CommandLineInterface& cli);

}
