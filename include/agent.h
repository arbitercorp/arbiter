#pragma once
// claudius/include/agent.h — Individual agent with conversation history + constitution

#include "constitution.h"
#include "api_client.h"
#include <string>
#include <vector>
#include <chrono>

namespace claudius {

struct AgentStats {
    int total_input_tokens  = 0;
    int total_output_tokens = 0;
    int total_requests      = 0;
    std::chrono::steady_clock::time_point created;
};

class Agent {
public:
    Agent(const std::string& id, Constitution config, ApiClient& client);

    // Send a message and get response (blocking)
    ApiResponse send(const std::string& user_message);

    // Send with streaming — chunks delivered via callback as they arrive
    ApiResponse stream(const std::string& user_message, StreamCallback cb);

    // Clear conversation history (keep constitution)
    void reset_history();

    // Truncate history to keep token usage bounded
    void trim_history(int keep_last_n = 10);

    // Accessors
    const std::string& id() const { return id_; }
    const Constitution& config() const { return config_; }
    Constitution& config_mut() { return config_; }
    const AgentStats& stats() const { return stats_; }
    const std::vector<Message>& history() const { return history_; }

    // Status summary (caveman-compressed)
    std::string status_summary() const;

    // Serialize/deserialize state
    std::string to_json() const;

private:
    std::string id_;
    Constitution config_;
    ApiClient& client_;
    std::vector<Message> history_;
    AgentStats stats_;
};

} // namespace claudius
