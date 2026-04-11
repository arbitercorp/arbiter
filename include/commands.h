#pragma once
// claudius/include/commands.h — Agent-invocable command execution

#include <string>
#include <vector>

namespace claudius {

struct AgentCommand {
    std::string name;  // "fetch", "mem"
    std::string args;  // rest of the command line
};

// Parse /command lines from an agent response.
// Skips lines inside ``` or ~~~ code fences.
std::vector<AgentCommand> parse_agent_commands(const std::string& response);

// Fetch a URL via curl. Returns content or "ERR: ..." on failure.
std::string cmd_fetch(const std::string& url);

// Read the agent's persistent memory file. Returns "" if none.
std::string cmd_mem_read(const std::string& agent_id, const std::string& memory_dir);

// Append a timestamped note to the agent's memory file.
void cmd_mem_write(const std::string& agent_id, const std::string& text,
                   const std::string& memory_dir);

// Delete the agent's memory file.
void cmd_mem_clear(const std::string& agent_id, const std::string& memory_dir);

// Execute a parsed command list and return a [TOOL RESULTS] message
// suitable for feeding back to the agent.
std::string execute_agent_commands(const std::vector<AgentCommand>& cmds,
                                   const std::string& agent_id,
                                   const std::string& memory_dir);

} // namespace claudius
