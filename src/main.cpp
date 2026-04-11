// claudius/src/main.cpp — Entry point
// Modes: interactive CLI, server (remote access), one-shot command
//
// Usage:
//   claudius                          — interactive REPL
//   claudius --serve [--port 9077]    — start TCP server
//   claudius --send <agent> <msg>     — one-shot message
//   claudius --init                   — generate token + example agents
//   claudius --gen-token              — generate new auth token

#include "orchestrator.h"
#include "commands.h"
#include "server.h"
#include "auth.h"
#include "constitution.h"
#include "readline_wrapper.h"
#include "cost_tracker.h"
#include "markdown.h"

#include <iostream>
#include <string>
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <queue>
#include <sstream>
#include <ctime>
#include <cstdio>
#include <unistd.h>
#include <sys/select.h>

namespace fs = std::filesystem;

static const char* BANNER =
    "\n"
    "Claudius                 v0.1.6\n"
    "\033[38;5;208m"
    "\n"
    "cbbbbbbbbbbbbbbbbbbbbbbbbbbbbbc\n"
    " cccbbbccccccccccccccccccccccc \n"
    "   cbbbbbbbbbbbbbbbbbcccc      \n"
    "     cbbbccccccccccc           \n"
    "     cbbbbbbbbbbbbbbbbccc      \n"
    "     cbbbccbbbccbbbbccbbb      \n"
    "     cbbc  bbb  bbb  cbbb      \n"
    "     cbbc  bbb  bbb  cbbb      \n"
    "     cbbc  bbb  bbb  cbbb      \n"
    "      cbc  bbb  bbb  cbc       \n"
    "            cc  cc             \n"
    "\033[0m"
    "\n"
    "   nemo omnibus horis sapit    \n"
    "\n";

static std::string agent_color(const std::string& agent_id) {
    if (agent_id == "claudius") return "\033[38;5;208m";  // orange

    // Palette: vivid, distinct 256-color codes
    static const int palette[] = {
        75,   // cornflower blue
        82,   // bright green
        171,  // magenta
        51,   // cyan
        226,  // yellow
        196,  // red
        141,  // violet
        214,  // dark orange
        85,   // seafoam
        207,  // pink
        39,   // sky blue
        154,  // lime
    };
    static const int palette_size = sizeof(palette) / sizeof(palette[0]);

    size_t h = std::hash<std::string>{}(agent_id);
    int code = palette[h % palette_size];

    char buf[32];
    std::snprintf(buf, sizeof(buf), "\033[38;5;%dm", code);
    return buf;
}

static std::string get_config_dir() {
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    std::string dir = std::string(home) + "/.claudius";
    fs::create_directories(dir);
    return dir;
}

static std::string get_memory_dir() {
    std::string dir = get_config_dir() + "/memory";
    fs::create_directories(dir);
    return dir;
}

// Thin wrappers so the interactive REPL can call the shared implementations.
static void write_memory(const std::string& agent_id, const std::string& text) {
    claudius::cmd_mem_write(agent_id, text, get_memory_dir());
}
static std::string read_memory(const std::string& agent_id) {
    return claudius::cmd_mem_read(agent_id, get_memory_dir());
}
static std::string fetch_url(const std::string& url) {
    return claudius::cmd_fetch(url);
}

static std::string get_api_key() {
    // Check env first, then config file
    const char* key = std::getenv("ANTHROPIC_API_KEY");
    if (key && key[0]) return key;

    std::string path = get_config_dir() + "/api_key";
    std::ifstream f(path);
    if (f.is_open()) {
        std::string k;
        std::getline(f, k);
        if (!k.empty()) return k;
    }

    std::cerr << "ERR: Set ANTHROPIC_API_KEY or write key to ~/.claudius/api_key\n";
    std::exit(1);
}

static void cmd_init() {
    std::string dir = get_config_dir();
    std::string agents_dir = dir + "/agents";
    fs::create_directories(agents_dir);

    // Generate initial auth token
    claudius::Auth auth;
    std::string token_path = dir + "/auth_tokens";
    auth.load(token_path);

    std::string token = claudius::Auth::generate_token();
    auth.add_token(token);
    auth.save(token_path);

    std::cout << "Initialized ~/.claudius/\n";
    std::cout << "Auth token (save this): " << token << "\n";
    std::cout << "Tokens stored (hashed) in: " << token_path << "\n\n";

    // Create example agent configs
    {
        claudius::Constitution c;
        c.name = "reviewer";
        c.role = "code-reviewer";
        c.brevity = claudius::Brevity::Ultra;
        c.max_tokens = 512;
        c.temperature = 0.2;
        c.goal = "Inspect code. Identify defects. Prescribe remedies.";
        c.personality = "Senior engineer. Finds fault efficiently. "
                        "Praises only what deserves it.";
        c.rules = {
            "Defects first, style second.",
            "Prescribe the concrete fix, never vague counsel.",
            "If the code is sound, say so in one sentence and move on.",
        };
        c.save(agents_dir + "/reviewer.json");
    }
    {
        claudius::Constitution c;
        c.name = "researcher";
        c.role = "research-analyst";
        c.brevity = claudius::Brevity::Lite;
        c.max_tokens = 2048;
        c.temperature = 0.5;
        c.goal = "Research topics with depth. Synthesize findings. Distinguish fact from inference.";
        c.personality = "Meticulous, skeptical of hearsay, prefers primary sources. "
                        "Reports with the formality of a written brief.";
        c.rules = {
            "Note confidence: high, medium, or low.",
            "Separate what is known from what is inferred.",
            "When uncertain, state it plainly.",
        };
        c.save(agents_dir + "/researcher.json");
    }
    {
        claudius::Constitution c;
        c.name = "devops";
        c.role = "infrastructure-engineer";
        c.brevity = claudius::Brevity::Full;
        c.max_tokens = 1024;
        c.temperature = 0.2;
        c.goal = "Build and maintain infrastructure. Debug failures. Automate the repeatable.";
        c.personality = "Ops veteran who has seen every manner of outage. "
                        "Paranoid about uptime. Trusts declarative systems over manual labor.";
        c.rules = {
            "Consider failure modes before all else.",
            "Prescribe monitoring and alerting for every change.",
            "Prefer the declarative over the imperative.",
            "If the action touches production, warn explicitly.",
        };
        c.save(agents_dir + "/devops.json");
    }

    std::cout << "Example agents created in " << agents_dir << "/\n";
    std::cout << "  reviewer.json   — code review (ultra)\n";
    std::cout << "  researcher.json — research analyst (lite)\n";
    std::cout << "  devops.json     — infrastructure (full)\n\n";
    std::cout << "Edit these or add your own. Then run: claudius\n";
}

static void cmd_gen_token() {
    std::string dir = get_config_dir();
    std::string token_path = dir + "/auth_tokens";

    claudius::Auth auth;
    auth.load(token_path);

    std::string token = claudius::Auth::generate_token();
    auth.add_token(token);
    auth.save(token_path);

    std::cout << "New token: " << token << "\n";
    std::cout << "Total active tokens: " << auth.token_count() << "\n";
}

static volatile sig_atomic_t g_running = 1;
static void signal_handler(int) { g_running = 0; }

static void cmd_serve(int port) {
    std::string dir = get_config_dir();
    std::string api_key = get_api_key();

    claudius::Orchestrator orch(api_key);
    orch.set_memory_dir(get_memory_dir());
    orch.load_agents(dir + "/agents");

    claudius::Auth auth;
    auth.load(dir + "/auth_tokens");

    if (auth.token_count() == 0) {
        std::cerr << "WARN: No auth tokens. Run: claudius --init\n";
    }

    claudius::Server server(orch, auth, port);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << BANNER;
    std::cout << "Server listening on port " << port << "\n";
    std::cout << "Agents loaded: " << orch.list_agents().size() << "\n";
    for (auto& id : orch.list_agents()) {
        std::cout << "  - " << id << "\n";
    }
    std::cout << "\nConnect: nc <host> " << port << "\n";
    std::cout << "Then: AUTH <token>\n\n";

    server.start();

    while (g_running && server.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\nShutting down...\n";
    server.stop();
    std::cout << "Final stats: " << orch.global_status() << "\n";
}

class ThinkingIndicator {
public:
    void start() {
        running_ = true;
        thread_ = std::thread([this]() {
            static const char* frames[] = {"   ", ".  ", ".. ", "..."};
            int i = 0;
            while (running_.load()) {
                std::cout << "\rthinking" << frames[i % 4] << std::flush;
                ++i;
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }
            std::cout << "\r            \r" << std::flush;
        });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// Shared output lock: prevents loop thread output from interleaving with
// interactive responses printed by the main thread.
static std::mutex g_out_mu;

// ─── Agent loop machinery ────────────────────────────────────────────────────

enum class LoopState { Running, Suspended, Stopped };

static const char* loop_state_str(LoopState s) {
    switch (s) {
        case LoopState::Running:   return "running";
        case LoopState::Suspended: return "suspended";
        case LoopState::Stopped:   return "stopped";
    }
    return "?";
}

struct LoopEntry {
    std::string loop_id;
    std::string agent_id;
    LoopState   state   = LoopState::Running;
    int         iter    = 0;
    std::string last_output;
    std::chrono::steady_clock::time_point started;

    std::mutex              mu;
    std::condition_variable cv;
    std::queue<std::string> injected;
    bool stop_req    = false;
    bool suspend_req = false;

    // Buffered output — loop runs silently; pull via /log <id>
    std::vector<std::string> output_log;

    // Terminal-visible error message set when loop exits abnormally
    std::string stop_reason;

    // Optional cost tracker for per-loop cost accounting
    claudius::CostTracker* tracker = nullptr;

    std::thread thread;

    LoopEntry() = default;
    LoopEntry(const LoopEntry&) = delete;
    LoopEntry& operator=(const LoopEntry&) = delete;
};

class LoopManager {
public:
    ~LoopManager() {
        std::unique_lock<std::mutex> lk(mu_);
        for (auto& [id, e] : loops_) {
            std::lock_guard<std::mutex> ek(e->mu);
            e->stop_req = true;
            e->cv.notify_all();
        }
        // Collect threads to join outside the lock
        std::vector<std::thread*> threads;
        for (auto& [id, e] : loops_) threads.push_back(&e->thread);
        lk.unlock();
        for (auto* t : threads) if (t->joinable()) t->join();
    }

    // Start a new loop; returns the loop ID.
    std::string start(claudius::Orchestrator& orch,
                      const std::string& agent_id,
                      const std::string& initial_prompt,
                      claudius::CostTracker* tracker = nullptr) {
        std::lock_guard<std::mutex> lk(mu_);
        std::string lid = "loop-" + std::to_string(next_id_++);
        auto e = std::make_unique<LoopEntry>();
        e->loop_id  = lid;
        e->agent_id = agent_id;
        e->started  = std::chrono::steady_clock::now();
        e->state    = LoopState::Running;
        e->tracker  = tracker;
        e->thread   = std::thread(run_loop, e.get(), std::ref(orch), initial_prompt);
        loops_[lid] = std::move(e);
        return lid;
    }

    bool kill(const std::string& lid) {
        std::unique_lock<std::mutex> lk(mu_);
        auto it = loops_.find(lid);
        if (it == loops_.end()) return false;
        auto* e = it->second.get();
        { std::lock_guard<std::mutex> ek(e->mu); e->stop_req = true; }
        e->cv.notify_all();
        auto& t = e->thread;
        lk.unlock();
        if (t.joinable()) t.join();
        lk.lock();
        loops_.erase(lid);
        return true;
    }

    bool suspend(const std::string& lid) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = loops_.find(lid);
        if (it == loops_.end()) return false;
        if (it->second->state != LoopState::Running) return false;
        { std::lock_guard<std::mutex> ek(it->second->mu); it->second->suspend_req = true; }
        it->second->state = LoopState::Suspended;
        return true;
    }

    bool resume(const std::string& lid) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = loops_.find(lid);
        if (it == loops_.end()) return false;
        if (it->second->state != LoopState::Suspended) return false;
        { std::lock_guard<std::mutex> ek(it->second->mu); it->second->suspend_req = false; }
        it->second->state = LoopState::Running;
        it->second->cv.notify_all();
        return true;
    }

    bool inject(const std::string& lid, const std::string& msg) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = loops_.find(lid);
        if (it == loops_.end()) return false;
        { std::lock_guard<std::mutex> ek(it->second->mu); it->second->injected.push(msg); }
        it->second->cv.notify_all();
        return true;
    }

    std::string list() const {
        std::lock_guard<std::mutex> lk(mu_);
        if (loops_.empty()) return "  (no active loops)\n";
        std::ostringstream ss;
        auto now = std::chrono::steady_clock::now();
        for (auto& [lid, e] : loops_) {
            long secs = std::chrono::duration_cast<std::chrono::seconds>(
                now - e->started).count();
            ss << "  " << lid
               << "  agent:" << e->agent_id
               << "  state:" << loop_state_str(e->state)
               << "  iter:" << e->iter
               << "  elapsed:" << secs << "s\n";
            if (e->state == LoopState::Stopped && !e->stop_reason.empty()) {
                ss << "    stop: " << e->stop_reason << "\n";
            } else if (!e->last_output.empty()) {
                // Show truncated last output as preview
                std::string preview = e->last_output.substr(
                    0, std::min<size_t>(120, e->last_output.size()));
                // Replace newlines with spaces for single-line preview
                for (char& c : preview) if (c == '\n') c = ' ';
                ss << "    last: " << preview;
                if (e->last_output.size() > 120) ss << "...";
                ss << "\n";
            }
        }
        return ss.str();
    }

    // Return buffered log for a loop. N=0 means all.
    std::string log(const std::string& lid, int last_n = 0) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = loops_.find(lid);
        if (it == loops_.end()) return "ERR: no loop '" + lid + "'\n";
        const auto& entries = it->second->output_log;
        if (entries.empty()) return "  (no output yet)\n";

        std::ostringstream ss;
        int start = 0;
        if (last_n > 0 && static_cast<int>(entries.size()) > last_n)
            start = static_cast<int>(entries.size()) - last_n;
        for (int i = start; i < static_cast<int>(entries.size()); ++i)
            ss << entries[i];
        return ss.str();
    }

    // Number of log entries (for tail-detection in /watch)
    size_t log_count(const std::string& lid) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = loops_.find(lid);
        if (it == loops_.end()) return 0;
        return it->second->output_log.size();
    }

    // Return log entries starting at offset (for streaming new entries)
    std::string log_since(const std::string& lid, size_t offset) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = loops_.find(lid);
        if (it == loops_.end()) return "";
        const auto& entries = it->second->output_log;
        std::ostringstream ss;
        for (size_t i = offset; i < entries.size(); ++i)
            ss << entries[i];
        return ss.str();
    }

    // True if the loop is stopped or no longer exists
    bool is_stopped(const std::string& lid) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = loops_.find(lid);
        if (it == loops_.end()) return true;
        return it->second->state == LoopState::Stopped;
    }

    // Reap any loops whose threads have finished
    void reap_stopped() {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = loops_.begin(); it != loops_.end(); ) {
            if (it->second->state == LoopState::Stopped) {
                if (it->second->thread.joinable()) it->second->thread.join();
                it = loops_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Return all loop IDs (for tab completion)
    std::vector<std::string> list_ids() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<std::string> ids;
        for (auto& [lid, _] : loops_) ids.push_back(lid);
        return ids;
    }

    bool has_active() const {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [_, e] : loops_)
            if (e->state != LoopState::Stopped) return true;
        return false;
    }

    int active_count() const {
        std::lock_guard<std::mutex> lk(mu_);
        int n = 0;
        for (auto& [_, e] : loops_)
            if (e->state != LoopState::Stopped) ++n;
        return n;
    }

private:
    static void run_loop(LoopEntry* e, claudius::Orchestrator& orch,
                         std::string initial_prompt) {
        // First iteration uses the initial prompt.
        // Subsequent iterations send "Continue." — the agent already has its
        // prior output in conversation history, so feeding the raw output back
        // as a user message would make it appear to be echoed by a human.
        std::string prompt = initial_prompt;
        bool first = true;
        bool stopped_by_request = false;
        int  consecutive_idle = 0;         // iterations with no tool calls
        int  total_iters      = 0;
        static constexpr int kMaxIdle  = 2;   // stop after this many consecutive idle turns
        static constexpr int kMaxIters = 20;  // hard ceiling

        while (true) {
            // Enforce hard iteration ceiling
            if (total_iters >= kMaxIters) {
                e->stop_reason = "max iterations reached (" + std::to_string(kMaxIters) + ")";
                {
                    std::lock_guard<std::mutex> out(g_out_mu);
                    std::cout << "\n\033[33m[" << e->loop_id << "/" << e->agent_id
                              << " MAX ITERS]\033[0m " << e->stop_reason
                              << "\n  Use /log " << e->loop_id << " to review.\n";
                    std::cout.flush();
                }
                break;
            }

            // Wait out any suspension (or stop)
            {
                std::unique_lock<std::mutex> lk(e->mu);
                e->cv.wait(lk, [e]{ return !e->suspend_req || e->stop_req; });
                if (e->stop_req) { stopped_by_request = true; break; }
                // Injected prompt resets the idle counter and resumes active work
                if (!e->injected.empty()) {
                    prompt = e->injected.front();
                    e->injected.pop();
                    first = true;
                    consecutive_idle = 0;
                }
            }

            // Log that this iteration is starting (visible via /watch before the API call returns)
            {
                std::ostringstream pre;
                pre << "[" << e->loop_id << "/" << e->agent_id
                    << " thinking...]\n";
                std::lock_guard<std::mutex> ek(e->mu);
                e->output_log.push_back(pre.str());
            }

            auto resp = orch.send(e->agent_id, prompt);
            e->iter++;
            total_iters++;

            // Track idle iterations (no tool calls = agent has nothing active to do)
            if (resp.ok) {
                if (resp.had_tool_calls) {
                    consecutive_idle = 0;
                } else {
                    consecutive_idle++;
                }
            }

            // Replace the "thinking..." placeholder with the real result
            {
                std::ostringstream entry;
                entry << "[" << e->loop_id << "/" << e->agent_id
                      << " #" << e->iter << "]\n";
                if (resp.ok) {
                    entry << claudius::render_markdown(resp.content) << "\n";
                    if (e->tracker) {
                        std::string model = orch.get_agent_model(e->agent_id);
                        e->tracker->record(e->agent_id, model, resp);
                        entry << "  " << e->tracker->format_footer(resp, model) << "\n";
                    } else {
                        entry << "  [in:" << resp.input_tokens
                              << " out:" << resp.output_tokens << "]\n";
                    }
                    e->last_output = resp.content;
                } else {
                    entry << "ERR: " << resp.error << "\n";
                }
                std::lock_guard<std::mutex> ek(e->mu);
                // Replace the last entry (the "thinking..." placeholder)
                if (!e->output_log.empty()) {
                    e->output_log.back() = entry.str();
                } else {
                    e->output_log.push_back(entry.str());
                }
            }

            if (!resp.ok) {
                e->stop_reason = resp.error;
                // Notify the user immediately — loop died
                {
                    std::lock_guard<std::mutex> out(g_out_mu);
                    std::cout << "\n\033[1;31m[" << e->loop_id << "/"
                              << e->agent_id << " FAILED]\033[0m "
                              << resp.error
                              << "\n  Use /log " << e->loop_id
                              << " to see output, /kill " << e->loop_id
                              << " to dismiss.\n";
                    std::cout.flush();
                }
                break;
            }

            // Auto-stop: agent has nothing left to do (no tool calls for N turns)
            if (consecutive_idle >= kMaxIdle) {
                e->stop_reason = "task complete (idle after " +
                                 std::to_string(consecutive_idle) + " turns)";
                {
                    std::lock_guard<std::mutex> out(g_out_mu);
                    std::cout << "\n\033[1;32m[" << e->loop_id << "/"
                              << e->agent_id << " DONE]\033[0m "
                              << e->stop_reason
                              << "\n  Use /log " << e->loop_id
                              << " to review, /kill " << e->loop_id << " to dismiss.\n";
                    std::cout.flush();
                }
                break;
            }

            // Don't feed output back as prompt — that causes the agent to
            // believe its own text is being echoed by a human, producing
            // infinite confusion. History already carries the prior response.
            if (first) {
                first = false;
            }
            prompt = "Continue.";

            // Re-check stop before sleeping
            { std::lock_guard<std::mutex> lk(e->mu); if (e->stop_req) { stopped_by_request = true; break; } }

            // Brief pause between iterations to avoid hammering the API
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        e->state = LoopState::Stopped;
    }

    mutable std::mutex mu_;
    std::map<std::string, std::unique_ptr<LoopEntry>> loops_;
    int next_id_ = 0;
};

static void cmd_interactive() {
    std::string dir = get_config_dir();
    std::string api_key = get_api_key();

    claudius::Orchestrator orch(api_key);
    orch.set_memory_dir(get_memory_dir());
    orch.load_agents(dir + "/agents");

    std::cout << BANNER;
    std::cout << "Basic Commands: /send <agent> <msg>\n";
    std::cout << "                /ask  <query>\n";
    std::cout << "                /use  <agent>\n";
    std::cout << "                /list\n";
    std::cout << "                /status\n";
    std::cout << "                /tokens\n";
    std::cout << "                /help\n";
    std::cout << "                /quit\n";
    std::cout << "\n";

    std::string current_agent = "claudius";
    ThinkingIndicator thinking;
    LoopManager loops;
    claudius::ReadlineWrapper rl;
    claudius::CostTracker tracker;
    bool exit_warned = false;

    rl.set_max_history(1000);
    rl.load_history(get_config_dir() + "/history");

    // Tab completion: slash commands, agent names, loop IDs
    rl.set_completion_provider(
        [&orch, &loops](const std::string& buf, const std::string& tok)
        -> std::vector<std::string> {
            auto match = [&](const std::vector<std::string>& candidates) {
                std::vector<std::string> out;
                for (auto& c : candidates)
                    if (c.substr(0, tok.size()) == tok) out.push_back(c);
                return out;
            };

            // Extract first word from buffer (the command)
            std::string cmd;
            {
                std::istringstream iss(buf);
                iss >> cmd;
                if (!cmd.empty() && cmd[0] == '/') cmd = cmd.substr(1);
            }
            bool only_cmd = (buf.find(' ') == std::string::npos);

            // Completing the slash command itself
            if (only_cmd || buf.empty()) {
                return match({"/send","/ask","/use","/list","/status","/tokens",
                              "/create","/remove","/reset","/model",
                              "/loop","/loops","/log","/watch",
                              "/kill","/suspend","/resume","/inject",
                              "/fetch","/mem","/quit","/help"});
            }

            // Agent name completion
            if (cmd == "send" || cmd == "use" || cmd == "loop" || cmd == "model") {
                auto agents = orch.list_agents();
                agents.push_back("claudius");
                return match(agents);
            }

            // Loop ID completion
            if (cmd == "kill"    || cmd == "suspend" || cmd == "resume" ||
                cmd == "watch"   || cmd == "log"     || cmd == "inject") {
                return match(loops.list_ids());
            }

            // /mem subcommands
            if (cmd == "mem") {
                return match({"write","read","show","clear"});
            }

            return {};
        });

    while (true) {
        std::string prompt =
            agent_color(current_agent)
            + "[" + current_agent + "]"
            + "\033[0m > ";

        std::string line;
        if (!rl.read_line(prompt, line)) {
            std::cout << "\n";
            break;
        }
        if (line.empty()) continue;

        // Intercept bare exit/quit commands (no slash required)
        {
            std::string lower = line;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            // trim whitespace
            while (!lower.empty() && lower.back()  == ' ') lower.pop_back();
            while (!lower.empty() && lower.front() == ' ') lower.erase(lower.begin());

            if (lower == "exit" || lower == "quit" || lower == "q" ||
                lower == "bye"  || lower == ":q") {
                if (loops.has_active() && !exit_warned) {
                    std::cout << "WARN: " << loops.active_count()
                              << " loop(s) still running.\n";
                    std::cout << "Type 'exit' again to force quit, or /loops to review.\n";
                    exit_warned = true;
                    continue;
                }
                break;
            } else {
                exit_warned = false;
            }
        }

        if (line[0] == '/') {
            // Parse command
            std::istringstream iss(line.substr(1));
            std::string cmd;
            iss >> cmd;

            if (cmd == "quit" || cmd == "exit" || cmd == "q") break;

            if (cmd == "list") {
                for (auto& id : orch.list_agents())
                    std::cout << "  " << id << "\n";
                continue;
            }
            if (cmd == "status") {
                std::cout << orch.global_status();
                continue;
            }
            if (cmd == "tokens") {
                std::cout << tracker.format_summary();
                continue;
            }
            if (cmd == "use" || cmd == "switch") {
                std::string id;
                iss >> id;
                if (id == "claudius" || orch.has_agent(id)) {
                    current_agent = id;
                    std::cout << "Switched to: " << id << "\n";
                } else {
                    std::cout << "ERR: no agent '" << id << "'\n";
                }
                continue;
            }
            if (cmd == "send") {
                std::string id;
                iss >> id;
                std::string msg;
                std::getline(iss, msg);
                if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
                try {
                    claudius::MarkdownRenderer md;
                    std::lock_guard<std::mutex> out(g_out_mu);
                    auto resp = orch.send_streaming(id, msg,
                        [&md](const std::string& chunk) {
                            auto s = md.feed(chunk);
                            if (!s.empty()) std::cout << s << std::flush;
                        });
                    auto tail = md.flush();
                    if (!tail.empty()) std::cout << tail;
                    std::cout << "\n";
                    if (resp.ok) {
                        tracker.record(id, orch.get_agent_model(id), resp);
                        std::cout << "  " << tracker.format_footer(resp, orch.get_agent_model(id)) << "\n";
                    } else {
                        std::cout << "ERR: " << resp.error << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cout << "ERR: " << e.what() << "\n";
                }
                continue;
            }
            if (cmd == "ask") {
                std::string query;
                std::getline(iss, query);
                if (!query.empty() && query[0] == ' ') query.erase(0, 1);
                try {
                    claudius::MarkdownRenderer md;
                    std::lock_guard<std::mutex> out(g_out_mu);
                    auto resp = orch.send_streaming("claudius", query,
                        [&md](const std::string& chunk) {
                            auto s = md.feed(chunk);
                            if (!s.empty()) std::cout << s << std::flush;
                        });
                    auto tail = md.flush();
                    if (!tail.empty()) std::cout << tail;
                    std::cout << "\n";
                    if (resp.ok) {
                        tracker.record("claudius", orch.get_agent_model("claudius"), resp);
                        std::cout << "  " << tracker.format_footer(resp, orch.get_agent_model("claudius")) << "\n";
                    } else {
                        std::cout << "ERR: " << resp.error << "\n";
                    }
                } catch (const std::exception& e) {
                    std::cout << "ERR: " << e.what() << "\n";
                }
                continue;
            }
            if (cmd == "create") {
                std::string id;
                iss >> id;
                try {
                    auto config = claudius::master_constitution();
                    config.name = id;
                    orch.create_agent(id, std::move(config));
                    std::cout << "Created: " << id << " (default config)\n";
                    std::cout << "Edit ~/.claudius/agents/" << id << ".json to customize\n";
                } catch (const std::exception& e) {
                    std::cout << "ERR: " << e.what() << "\n";
                }
                continue;
            }
            if (cmd == "remove") {
                std::string id;
                iss >> id;
                orch.remove_agent(id);
                std::cout << "Removed: " << id << "\n";
                if (current_agent == id) current_agent = "claudius";
                continue;
            }
            if (cmd == "reset") {
                std::string id;
                iss >> id;
                if (id.empty()) id = current_agent;
                try {
                    orch.get_agent(id).reset_history();
                    std::cout << "History cleared: " << id << "\n";
                } catch (const std::exception& e) {
                    std::cout << "ERR: " << e.what() << "\n";
                }
                continue;
            }
            if (cmd == "loop") {
                std::string id;
                iss >> id;
                std::string prompt;
                std::getline(iss, prompt);
                if (!prompt.empty() && prompt[0] == ' ') prompt.erase(0, 1);
                if (id.empty() || prompt.empty()) {
                    std::cout << "Usage: /loop <agent> <initial prompt>\n";
                    continue;
                }
                if (id != "claudius" && !orch.has_agent(id)) {
                    std::cout << "ERR: no agent '" << id << "'\n";
                    continue;
                }
                std::string lid = loops.start(orch, id, prompt, &tracker);
                std::cout << "Loop started: " << lid << " (agent: " << id << ")\n";
                continue;
            }
            if (cmd == "loops") {
                std::cout << loops.list();
                continue;
            }
            if (cmd == "kill") {
                std::string lid;
                iss >> lid;
                if (loops.kill(lid))
                    std::cout << "Killed: " << lid << "\n";
                else
                    std::cout << "ERR: no loop '" << lid << "'\n";
                continue;
            }
            if (cmd == "suspend") {
                std::string lid;
                iss >> lid;
                if (loops.suspend(lid))
                    std::cout << "Suspended: " << lid << "\n";
                else
                    std::cout << "ERR: no loop '" << lid << "' or not running\n";
                continue;
            }
            if (cmd == "resume") {
                std::string lid;
                iss >> lid;
                if (loops.resume(lid))
                    std::cout << "Resumed: " << lid << "\n";
                else
                    std::cout << "ERR: no loop '" << lid << "' or not suspended\n";
                continue;
            }
            if (cmd == "inject") {
                std::string lid;
                iss >> lid;
                std::string msg;
                std::getline(iss, msg);
                if (!msg.empty() && msg[0] == ' ') msg.erase(0, 1);
                if (loops.inject(lid, msg))
                    std::cout << "Injected into " << lid << "\n";
                else
                    std::cout << "ERR: no loop '" << lid << "'\n";
                continue;
            }
            if (cmd == "log") {
                std::string lid;
                iss >> lid;
                if (lid.empty()) {
                    std::cout << "Usage: /log <loop-id> [last-N]\n";
                    continue;
                }
                int n = 0;
                iss >> n;
                std::cout << loops.log(lid, n);
                continue;
            }
            if (cmd == "watch") {
                std::string lid;
                iss >> lid;
                if (lid.empty()) {
                    std::cout << "Usage: /watch <loop-id>\n";
                    continue;
                }
                if (loops.is_stopped(lid) && loops.log_count(lid) == 0) {
                    std::cout << "ERR: no loop '" << lid << "'\n";
                    continue;
                }
                // Dump everything buffered so far
                size_t seen = loops.log_count(lid);
                std::cout << loops.log(lid, 0);
                if (!loops.is_stopped(lid)) {
                    std::cout << "\033[2m--- watching " << lid
                              << " — press Enter to detach ---\033[0m\n";
                    std::cout.flush();
                    // Tail new entries using select() for non-blocking stdin check
                    while (!loops.is_stopped(lid)) {
                        fd_set fds;
                        FD_ZERO(&fds);
                        FD_SET(STDIN_FILENO, &fds);
                        struct timeval tv{0, 200000}; // 200 ms poll
                        int rdy = ::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
                        if (rdy > 0) {
                            // User pressed Enter — return to REPL
                            std::string dummy;
                            std::getline(std::cin, dummy);
                            break;
                        }
                        // Print any new log entries
                        size_t now = loops.log_count(lid);
                        if (now > seen) {
                            std::lock_guard<std::mutex> out(g_out_mu);
                            std::cout << loops.log_since(lid, seen);
                            std::cout.flush();
                            seen = now;
                        }
                    }
                    // Flush any remaining entries after stop
                    size_t now = loops.log_count(lid);
                    if (now > seen) {
                        std::cout << loops.log_since(lid, seen);
                    }
                    if (loops.is_stopped(lid)) {
                        std::cout << "\033[2m--- loop finished ---\033[0m\n";
                    } else {
                        std::cout << "\033[2m--- detached ---\033[0m\n";
                    }
                }
                std::cout.flush();
                continue;
            }
            if (cmd == "fetch") {
                std::string url;
                iss >> url;
                if (url.empty()) {
                    std::cout << "Usage: /fetch <url>\n";
                    continue;
                }
                std::cout << "fetching " << url << "...\n";
                std::string content = fetch_url(url);
                if (content.substr(0, 4) == "ERR:") {
                    std::cout << content << "\n";
                    continue;
                }
                // Cap at 32 KB to avoid bloating the agent's context window
                static constexpr size_t kFetchLimit = 32768;
                if (content.size() > kFetchLimit) {
                    content.resize(kFetchLimit);
                    content += "\n... [content truncated to 32 KB]";
                }
                // Inject into current agent as a user message
                std::string msg = "[FETCHED: " + url + "]\n" + content +
                                  "\n[END FETCHED]\n";
                try {
                    thinking.start();
                    auto resp = orch.send(current_agent, msg);
                    thinking.stop();
                    std::lock_guard<std::mutex> out(g_out_mu);
                    if (resp.ok) {
                        std::cout << claudius::render_markdown(resp.content) << "\n";
                        tracker.record(current_agent, orch.get_agent_model(current_agent), resp);
                        std::cout << "  " << tracker.format_footer(resp, orch.get_agent_model(current_agent)) << "\n";
                    } else {
                        std::cout << "ERR: " << resp.error << "\n";
                    }
                } catch (const std::exception& ex) {
                    thinking.stop();
                    std::cout << "ERR: " << ex.what() << "\n";
                }
                continue;
            }
            if (cmd == "mem") {
                std::string subcmd;
                iss >> subcmd;
                if (subcmd == "write") {
                    std::string text;
                    std::getline(iss, text);
                    if (!text.empty() && text[0] == ' ') text.erase(0, 1);
                    if (text.empty()) {
                        std::cout << "Usage: /mem write <text>\n";
                        continue;
                    }
                    write_memory(current_agent, text);
                    std::cout << "Memory written for " << current_agent << "\n";
                } else if (subcmd == "read") {
                    std::string mem = read_memory(current_agent);
                    if (mem.empty()) {
                        std::cout << "No memory for " << current_agent << "\n";
                        continue;
                    }
                    // Inject memory into agent context
                    std::string msg = "[MEMORY for " + current_agent + "]:\n" +
                                      mem + "\n[END MEMORY]\n";
                    try {
                        thinking.start();
                        auto resp = orch.send(current_agent, msg);
                        thinking.stop();
                        std::lock_guard<std::mutex> out(g_out_mu);
                        if (resp.ok) {
                            std::cout << claudius::render_markdown(resp.content) << "\n";
                            tracker.record(current_agent, orch.get_agent_model(current_agent), resp);
                            std::cout << "  " << tracker.format_footer(resp, orch.get_agent_model(current_agent)) << "\n";
                        } else {
                            std::cout << "ERR: " << resp.error << "\n";
                        }
                    } catch (const std::exception& ex) {
                        thinking.stop();
                        std::cout << "ERR: " << ex.what() << "\n";
                    }
                } else if (subcmd == "show") {
                    std::string mem = read_memory(current_agent);
                    if (mem.empty())
                        std::cout << "No memory for " << current_agent << "\n";
                    else
                        std::cout << mem << "\n";
                } else if (subcmd == "clear") {
                    std::string path = get_memory_dir() + "/" + current_agent + ".md";
                    fs::remove(path);
                    std::cout << "Memory cleared for " << current_agent << "\n";
                } else {
                    std::cout << "Usage: /mem write <text> | /mem read | /mem show | /mem clear\n";
                }
                continue;
            }
            if (cmd == "model") {
                std::string id, model;
                iss >> id >> model;
                if (id.empty() || model.empty()) {
                    std::cout << "Usage: /model <agent-id> <model-id>\n";
                    std::cout << "  e.g. /model researcher claude-haiku-4-5-20251001\n";
                    continue;
                }
                try {
                    orch.get_agent(id).config_mut().model = model;
                    std::cout << id << " model -> " << model << "\n";
                } catch (const std::exception& ex) {
                    std::cout << "ERR: " << ex.what() << "\n";
                }
                continue;
            }
            if (cmd == "help") {
                std::cout << "Commands:\n"
                    "  /send <agent> <msg>              — send to specific agent\n"
                    "  /ask <query>                     — ask claudius master\n"
                    "  /use <agent>                     — switch current agent\n"
                    "  /list                            — list agents\n"
                    "  /status                          — system status\n"
                    "  /tokens                          — token usage\n"
                    "  /create <id>                     — create agent (default config)\n"
                    "  /remove <id>                     — remove agent\n"
                    "  /reset [id]                      — clear agent history\n"
                    "  /model <agent> <model-id>        — change agent model at runtime\n"
                    "\n"
                    "  /loop <agent> <prompt>           — run agent in background loop\n"
                    "  /loops                           — list running/suspended loops\n"
                    "  /log <loop-id> [last-N]          — show buffered loop output\n"
                    "  /watch <loop-id>                 — tail loop output live (Enter to detach)\n"
                    "  /kill <loop-id>                  — stop a loop\n"
                    "  /suspend <loop-id>               — pause a loop\n"
                    "  /resume <loop-id>                — resume a paused loop\n"
                    "  /inject <loop-id> <msg>          — send message into a running loop\n"
                    "\n"
                    "  /fetch <url>                     — fetch URL and send HTML to current agent\n"
                    "  /mem write <text>                — append note to agent memory\n"
                    "  /mem read                        — load memory into agent context\n"
                    "  /mem show                        — print raw memory file\n"
                    "  /mem clear                       — delete agent memory file\n"
                    "\n"
                    "  /quit                            — exit\n"
                    "\n"
                    "Plain text sends to current agent.\n";
                continue;
            }

            std::cout << "Unknown command. /help for list.\n";
            continue;
        }

        // Plain text → stream to current agent
        try {
            claudius::MarkdownRenderer md;
            std::lock_guard<std::mutex> out(g_out_mu);
            auto resp = orch.send_streaming(current_agent, line,
                [&md](const std::string& chunk) {
                    auto s = md.feed(chunk);
                    if (!s.empty()) std::cout << s << std::flush;
                });
            auto tail = md.flush();
            if (!tail.empty()) std::cout << tail;
            std::cout << "\n";
            if (resp.ok) {
                tracker.record(current_agent, orch.get_agent_model(current_agent), resp);
                std::cout << "  " << tracker.format_footer(resp, orch.get_agent_model(current_agent)) << "\n";
            } else {
                std::cout << "ERR: " << resp.error << "\n";
            }
        } catch (const std::exception& e) {
            std::cout << "ERR: " << e.what() << "\n";
        }
    }

    // Session summary on exit
    std::cout << "\n";
    if (tracker.session_cost() > 0.0) {
        std::cout << tracker.format_summary();
    }
    std::cout << orch.global_status();
}

static void cmd_oneshot(const std::string& agent_id, const std::string& msg) {
    std::string dir = get_config_dir();
    std::string api_key = get_api_key();

    claudius::Orchestrator orch(api_key);
    orch.load_agents(dir + "/agents");

    auto resp = orch.send(agent_id, msg);
    if (resp.ok) {
        std::cout << resp.content << "\n";
    } else {
        std::cerr << "ERR: " << resp.error << "\n";
        std::exit(1);
    }
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            cmd_interactive();
            return 0;
        }

        std::string arg1 = argv[1];

        if (arg1 == "--init" || arg1 == "init") {
            cmd_init();
            return 0;
        }
        if (arg1 == "--gen-token" || arg1 == "gen-token") {
            cmd_gen_token();
            return 0;
        }
        if (arg1 == "--serve" || arg1 == "serve") {
            int port = 9077;
            if (argc >= 4 && std::string(argv[2]) == "--port") {
                port = std::atoi(argv[3]);
            }
            cmd_serve(port);
            return 0;
        }
        if (arg1 == "--send" || arg1 == "send") {
            if (argc < 4) {
                std::cerr << "Usage: claudius --send <agent_id> <message>\n";
                return 1;
            }
            std::string agent = argv[2];
            std::string msg;
            for (int i = 3; i < argc; ++i) {
                if (i > 3) msg += " ";
                msg += argv[i];
            }
            cmd_oneshot(agent, msg);
            return 0;
        }
        if (arg1 == "--help" || arg1 == "-h" || arg1 == "help") {
            std::cout << BANNER;
            std::cout <<
                "Usage:\n"
                "  claudius                          Interactive REPL\n"
                "  claudius --serve [--port N]        Start TCP server (default 9077)\n"
                "  claudius --send <agent> <msg>      One-shot message\n"
                "  claudius --init                    Initialize config + tokens\n"
                "  claudius --gen-token               Generate new auth token\n"
                "  claudius --help                    This help\n\n"
                "Environment:\n"
                "  ANTHROPIC_API_KEY                  Claude API key\n\n"
                "Config: ~/.claudius/\n"
                "  api_key                            API key file\n"
                "  auth_tokens                        Hashed access tokens\n"
                "  agents/*.json                      Agent constitutions\n";
            return 0;
        }

        std::cerr << "Unknown option: " << arg1 << ". Try --help\n";
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "FATAL: " << e.what() << "\n";
        return 1;
    }
}
