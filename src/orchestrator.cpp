// claudius/src/orchestrator.cpp
#include "orchestrator.h"
#include "commands.h"
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace claudius {

Orchestrator::Orchestrator(const std::string& api_key)
    : client_(api_key)
{
    // Default memory directory: ~/.claudius/memory
    const char* home = std::getenv("HOME");
    memory_dir_ = (home ? std::string(home) : std::string(".")) + "/.claudius/memory";

    // Create master Claudius agent
    auto master = master_constitution();
    claudius_master_ = std::make_unique<Agent>("claudius", master, client_);
}

Agent& Orchestrator::create_agent(const std::string& id, Constitution config) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    if (agents_.count(id)) {
        throw std::runtime_error("Agent already exists: " + id);
    }
    auto agent = std::make_unique<Agent>(id, std::move(config), client_);
    auto& ref = *agent;
    agents_[id] = std::move(agent);
    return ref;
}

Agent& Orchestrator::get_agent(const std::string& id) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    auto it = agents_.find(id);
    if (it == agents_.end()) throw std::runtime_error("No agent: " + id);
    return *it->second;
}

bool Orchestrator::has_agent(const std::string& id) const {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    return agents_.count(id) > 0;
}

void Orchestrator::remove_agent(const std::string& id) {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    agents_.erase(id);
}

std::vector<std::string> Orchestrator::list_agents() const {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    std::vector<std::string> ids;
    ids.reserve(agents_.size());
    for (auto& [id, _] : agents_) ids.push_back(id);
    return ids;
}

void Orchestrator::load_agents(const std::string& dir) {
    if (!fs::exists(dir)) return;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            try {
                auto config = Constitution::from_file(entry.path().string());
                std::string id = config.name.empty()
                    ? entry.path().stem().string()
                    : config.name;
                create_agent(id, std::move(config));
            } catch (const std::exception& e) {
                // Skip malformed agent files, log to stderr
                fprintf(stderr, "WARN: skip %s: %s\n",
                    entry.path().c_str(), e.what());
            }
        }
    }
}

ApiResponse Orchestrator::send(const std::string& agent_id, const std::string& message) {
    // Resolve agent pointer and build the first message.
    Agent* agent_ptr;
    std::string current_msg;

    if (agent_id == "claudius") {
        agent_ptr  = claudius_master_.get();
        current_msg = global_status() + "\n\nQUERY: " + message;
    } else {
        agent_ptr  = &get_agent(agent_id);
        current_msg = message;
    }

    // Agentic dispatch loop: execute /fetch and /mem commands emitted by the
    // agent and feed results back, up to 6 turns.
    ApiResponse resp;
    for (int i = 0; i < 6; ++i) {
        resp = agent_ptr->send(current_msg);
        if (!resp.ok) return resp;

        auto cmds = parse_agent_commands(resp.content);
        if (cmds.empty()) break;

        resp.had_tool_calls = true;
        current_msg = execute_agent_commands(cmds, agent_id, memory_dir_);
    }

    return resp;
}

ApiResponse Orchestrator::ask_claudius(const std::string& query) {
    return send("claudius", query);
}

std::string Orchestrator::global_status() const {
    std::lock_guard<std::mutex> lock(agents_mutex_);
    std::ostringstream ss;
    ss << "SYSTEM STATUS\n";
    ss << "agents:" << agents_.size()
       << " | total_in:" << client_.total_input_tokens()
       << " total_out:" << client_.total_output_tokens() << "\n";
    for (auto& [id, agent] : agents_) {
        ss << "  " << agent->status_summary() << "\n";
    }
    return ss.str();
}

} // namespace claudius
