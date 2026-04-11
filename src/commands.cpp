// claudius/src/commands.cpp — Agent-invocable command execution
#include "commands.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace claudius {

// ---------------------------------------------------------------------------
// parse_agent_commands
// ---------------------------------------------------------------------------

std::vector<AgentCommand> parse_agent_commands(const std::string& response) {
    std::vector<AgentCommand> result;
    std::istringstream ss(response);
    std::string line;
    bool in_code_block = false;

    while (std::getline(ss, line)) {
        // Track code fences (``` or ~~~)
        if (line.size() >= 3 &&
            (line.substr(0, 3) == "```" || line.substr(0, 3) == "~~~")) {
            in_code_block = !in_code_block;
            continue;
        }
        if (in_code_block) continue;

        // Trim trailing whitespace / CR
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r'))
            line.pop_back();

        if (line.size() > 7 && line.substr(0, 7) == "/fetch ") {
            AgentCommand cmd;
            cmd.name = "fetch";
            cmd.args = line.substr(7);
            if (!cmd.args.empty()) result.push_back(std::move(cmd));

        } else if (line.size() > 5 && line.substr(0, 5) == "/mem ") {
            AgentCommand cmd;
            cmd.name = "mem";
            cmd.args = line.substr(5);
            if (!cmd.args.empty()) result.push_back(std::move(cmd));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// cmd_fetch
// ---------------------------------------------------------------------------

std::string cmd_fetch(const std::string& url) {
    // Must start with http:// or https://
    if (url.substr(0, 7) != "http://" && url.substr(0, 8) != "https://")
        return "ERR: URL must start with http:// or https://";

    // Reject shell metacharacters to prevent injection
    for (char c : url) {
        if (c == '\'' || c == '"' || c == '`' || c == '$' ||
            c == ';'  || c == '&' || c == '|' || c == '>' ||
            c == '<'  || c == '\n'|| c == '\r') {
            return "ERR: URL contains invalid characters";
        }
    }

    std::string cmd = "curl -sL --max-time 15 --max-filesize 524288 '" + url + "' 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "ERR: failed to run curl";

    std::string result;
    result.reserve(65536);
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
        if (result.size() > 512 * 1024) break;
    }
    pclose(pipe);
    return result;
}

// ---------------------------------------------------------------------------
// Memory helpers
// ---------------------------------------------------------------------------

std::string cmd_mem_read(const std::string& agent_id, const std::string& memory_dir) {
    std::string path = memory_dir + "/" + agent_id + ".md";
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void cmd_mem_write(const std::string& agent_id, const std::string& text,
                   const std::string& memory_dir) {
    fs::create_directories(memory_dir);
    std::string path = memory_dir + "/" + agent_id + ".md";
    std::ofstream f(path, std::ios::app);
    if (!f.is_open()) return;

    std::time_t now = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    f << "\n<!-- " << ts << " -->\n" << text << "\n";
}

void cmd_mem_clear(const std::string& agent_id, const std::string& memory_dir) {
    std::string path = memory_dir + "/" + agent_id + ".md";
    fs::remove(path);
}

// ---------------------------------------------------------------------------
// execute_agent_commands
// ---------------------------------------------------------------------------

std::string execute_agent_commands(const std::vector<AgentCommand>& cmds,
                                   const std::string& agent_id,
                                   const std::string& memory_dir) {
    std::ostringstream out;
    out << "[TOOL RESULTS]\n";

    for (auto& cmd : cmds) {
        if (cmd.name == "fetch") {
            out << "[/fetch " << cmd.args << "]\n";
            out << cmd_fetch(cmd.args) << "\n";
            out << "[END FETCH]\n\n";

        } else if (cmd.name == "mem") {
            std::istringstream iss(cmd.args);
            std::string subcmd;
            iss >> subcmd;

            if (subcmd == "write") {
                std::string text;
                std::getline(iss, text);
                if (!text.empty() && text[0] == ' ') text.erase(0, 1);
                cmd_mem_write(agent_id, text, memory_dir);
                out << "[/mem write] OK: written\n\n";

            } else if (subcmd == "read") {
                std::string mem = cmd_mem_read(agent_id, memory_dir);
                out << "[/mem read]\n"
                    << (mem.empty() ? "(no memory)" : mem)
                    << "\n[END MEMORY]\n\n";

            } else if (subcmd == "show") {
                std::string mem = cmd_mem_read(agent_id, memory_dir);
                out << "[/mem show]\n"
                    << (mem.empty() ? "(no memory)" : mem)
                    << "\n[END MEMORY]\n\n";

            } else if (subcmd == "clear") {
                cmd_mem_clear(agent_id, memory_dir);
                out << "[/mem clear] OK: memory cleared\n\n";
            }
        }
    }

    out << "[END TOOL RESULTS]";
    return out.str();
}

} // namespace claudius
