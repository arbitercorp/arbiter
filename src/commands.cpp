// claudius/src/commands.cpp — Agent-invocable command execution
#include "commands.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
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

        } else if (line.size() > 7 && line.substr(0, 7) == "/agent ") {
            AgentCommand cmd;
            cmd.name = "agent";
            cmd.args = line.substr(7);
            if (!cmd.args.empty()) result.push_back(std::move(cmd));

        } else if (line.size() > 6 && line.substr(0, 6) == "/exec ") {
            AgentCommand cmd;
            cmd.name = "exec";
            cmd.args = line.substr(6);
            if (!cmd.args.empty()) result.push_back(std::move(cmd));
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// html_to_text — strip tags and boilerplate, return readable text
// ---------------------------------------------------------------------------

static std::string html_to_text(const std::string& html) {
    std::string out;
    out.reserve(html.size() / 4);

    size_t i = 0;
    const size_t n = html.size();

    // Skip script/style blocks wholesale
    auto skip_block = [&](const char* close_tag) {
        size_t pos = html.find(close_tag, i);
        i = (pos == std::string::npos) ? n : pos + std::strlen(close_tag);
    };

    bool last_was_space = true;

    while (i < n) {
        if (html[i] == '<') {
            // Peek at the tag name
            size_t j = i + 1;
            while (j < n && html[j] == ' ') ++j;
            // Check for block-level tags we want to map to newlines
            auto tag_is = [&](const char* t) {
                size_t tl = std::strlen(t);
                return n - j >= tl &&
                       ::strncasecmp(html.c_str() + j, t, tl) == 0 &&
                       (j + tl >= n || html[j + tl] == '>' || html[j + tl] == ' ');
            };
            if (tag_is("script") || tag_is("style") || tag_is("noscript")) {
                // Find the matching close tag
                size_t close = html.find('>', i);
                if (close != std::string::npos) i = close + 1;
                // Now skip until </script>, </style>, </noscript>
                if (tag_is("script"))   { skip_block("</script>");   continue; }
                if (tag_is("style"))    { skip_block("</style>");    continue; }
                if (tag_is("noscript")) { skip_block("</noscript>"); continue; }
            }
            bool block = tag_is("p")  || tag_is("/p")  ||
                         tag_is("br") || tag_is("li")  ||
                         tag_is("h1") || tag_is("h2")  || tag_is("h3") ||
                         tag_is("h4") || tag_is("h5")  || tag_is("h6") ||
                         tag_is("div") || tag_is("/div") ||
                         tag_is("tr") || tag_is("td")  || tag_is("th");
            // Skip to end of tag
            while (i < n && html[i] != '>') ++i;
            if (i < n) ++i;
            if (block && !out.empty() && out.back() != '\n') {
                out += '\n';
                last_was_space = true;
            }
        } else if (html[i] == '&') {
            // Basic HTML entity decoding
            if (n - i >= 4 && html.substr(i, 4) == "&lt;")       { out += '<'; i += 4; }
            else if (n - i >= 4 && html.substr(i, 4) == "&gt;")  { out += '>'; i += 4; }
            else if (n - i >= 5 && html.substr(i, 5) == "&amp;") { out += '&'; i += 5; }
            else if (n - i >= 6 && html.substr(i, 6) == "&nbsp;"){ out += ' '; i += 6; last_was_space = true; }
            else { out += html[i++]; last_was_space = false; }
        } else {
            char c = html[i++];
            if (c == '\r') continue;
            bool is_ws = (c == ' ' || c == '\t' || c == '\n');
            if (is_ws) {
                if (!last_was_space && !out.empty()) {
                    out += (c == '\n') ? '\n' : ' ';
                    last_was_space = true;
                }
            } else {
                out += c;
                last_was_space = false;
            }
        }
    }

    // Collapse runs of blank lines to a single blank line
    std::string compressed;
    compressed.reserve(out.size());
    int consecutive_newlines = 0;
    for (char c : out) {
        if (c == '\n') {
            ++consecutive_newlines;
            if (consecutive_newlines <= 2) compressed += c;
        } else {
            consecutive_newlines = 0;
            compressed += c;
        }
    }

    return compressed;
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

    std::string raw;
    raw.reserve(65536);
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        raw += buf;
        if (raw.size() > 512 * 1024) break;
    }
    pclose(pipe);

    // Strip HTML tags and boilerplate to save tokens
    std::string result = html_to_text(raw);
    return result;
}

// ---------------------------------------------------------------------------
// cmd_exec
// ---------------------------------------------------------------------------

std::string cmd_exec(const std::string& command) {
    static constexpr size_t kMaxOutput = 32768;

    if (command.empty()) return "ERR: empty command";

    // Capture stdout and stderr together
    std::string shell_cmd = command + " 2>&1";
    FILE* pipe = popen(shell_cmd.c_str(), "r");
    if (!pipe) return "ERR: popen failed";

    std::string output;
    output.reserve(4096);
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
        if (output.size() > kMaxOutput) {
            output.resize(kMaxOutput);
            output += "\n... [truncated at 32 KB]";
            break;
        }
    }
    int status = pclose(pipe);

    // Trim trailing newlines
    while (!output.empty() && output.back() == '\n')
        output.pop_back();

    if (output.empty()) output = "(no output)";

    if (status != 0) {
        output += "\n[exit " + std::to_string(status) + "]";
    }

    return output;
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
                                   const std::string& memory_dir,
                                   AgentInvoker agent_invoker) {
    std::ostringstream out;
    out << "[TOOL RESULTS]\n";

    // Caps: 16 KB per fetch (stripped text), max 3 fetches per turn,
    // and a total tool-result budget of 32 KB.
    static constexpr size_t kPerFetchLimit  = 16384;
    static constexpr size_t kTotalLimit     = 32768;
    static constexpr int    kMaxFetches     = 3;
    int fetch_count = 0;

    for (auto& cmd : cmds) {
        // Enforce total budget
        if (out.tellp() >= static_cast<std::streampos>(kTotalLimit)) {
            out << "[TOOL RESULTS TRUNCATED: budget exhausted]\n";
            break;
        }

        if (cmd.name == "fetch") {
            if (fetch_count >= kMaxFetches) {
                out << "[/fetch " << cmd.args << "]\n"
                    << "SKIPPED: max " << kMaxFetches << " fetches per turn\n"
                    << "[END FETCH]\n\n";
                continue;
            }
            ++fetch_count;
            out << "[/fetch " << cmd.args << "]\n";
            std::string fetched = cmd_fetch(cmd.args);
            if (fetched.size() > kPerFetchLimit) {
                fetched.resize(kPerFetchLimit);
                fetched += "\n... [truncated]";
            }
            out << fetched << "\n";
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

        } else if (cmd.name == "exec") {
            out << "[/exec " << cmd.args << "]\n";
            out << cmd_exec(cmd.args) << "\n";
            out << "[END EXEC]\n\n";

        } else if (cmd.name == "agent") {
            // /agent <sub_agent_id> <message>
            std::istringstream iss(cmd.args);
            std::string sub_id;
            iss >> sub_id;
            std::string sub_msg;
            std::getline(iss, sub_msg);
            if (!sub_msg.empty() && sub_msg[0] == ' ') sub_msg.erase(0, 1);

            out << "[/agent " << sub_id << "]\n";
            if (agent_invoker) {
                out << agent_invoker(sub_id, sub_msg) << "\n";
            } else {
                out << "ERR: agent invocation unavailable in this context\n";
            }
            out << "[END AGENT]\n\n";
        }
    }

    out << "[END TOOL RESULTS]";
    return out.str();
}

} // namespace claudius
