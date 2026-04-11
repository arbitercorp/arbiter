// claudius/src/readline_wrapper.cpp — readline / libedit wrapper

#include "readline_wrapper.h"

#include <cstring>
#include <cstdlib>

#ifdef CLAUDIUS_HAS_READLINE
#  ifdef CLAUDIUS_USE_EDITLINE
#    include <editline/readline.h>
#  else
#    include <readline/readline.h>
#    include <readline/history.h>
#  endif
#else
#  include <iostream>
#endif

namespace claudius {

// ─── Global state (readline uses C callbacks) ────────────────────────────────

#ifdef CLAUDIUS_HAS_READLINE
static CompletionProvider g_provider;

static char* completion_generator(const char* text, int state) {
    static std::vector<std::string> matches;
    static size_t idx = 0;

    if (state == 0) {
        if (g_provider) {
            matches = g_provider(
                std::string(rl_line_buffer),
                std::string(text));
        } else {
            matches.clear();
        }
        idx = 0;
    }
    if (idx >= matches.size()) return nullptr;
    return ::strdup(matches[idx++].c_str());
}

static char** completion_callback(const char* text, int /*start*/, int /*end*/) {
    rl_attempted_completion_over = 1;  // suppress filename fallback
    if (!g_provider) return nullptr;
    return rl_completion_matches(text, completion_generator);
}
#endif

// ─── ReadlineWrapper ─────────────────────────────────────────────────────────

ReadlineWrapper::ReadlineWrapper() {
#ifdef CLAUDIUS_HAS_READLINE
    rl_attempted_completion_function = completion_callback;
    rl_completion_append_character   = ' ';
#endif
}

ReadlineWrapper::~ReadlineWrapper() {
    if (!history_path_.empty()) {
        save_history(history_path_);
    }
}

bool ReadlineWrapper::read_line(const std::string& prompt, std::string& out) {
#ifdef CLAUDIUS_HAS_READLINE
    char* raw = ::readline(prompt.c_str());
    if (!raw) return false;  // EOF / Ctrl-D
    out = raw;
    if (!out.empty()) ::add_history(raw);
    ::free(raw);
    return true;
#else
    std::cout << prompt;
    std::cout.flush();
    return static_cast<bool>(std::getline(std::cin, out));
#endif
}

void ReadlineWrapper::set_completion_provider(CompletionProvider provider) {
#ifdef CLAUDIUS_HAS_READLINE
    g_provider = std::move(provider);
#else
    (void)provider;
#endif
}

void ReadlineWrapper::load_history(const std::string& path) {
    history_path_ = path;
#ifdef CLAUDIUS_HAS_READLINE
    ::read_history(path.c_str());  // silently ignored if file doesn't exist
#endif
}

void ReadlineWrapper::save_history(const std::string& path) {
#ifdef CLAUDIUS_HAS_READLINE
    if (max_history_ > 0) ::stifle_history(max_history_);
    ::write_history(path.c_str());
#else
    (void)path;
#endif
}

void ReadlineWrapper::set_max_history(int n) {
    max_history_ = n;
#ifdef CLAUDIUS_HAS_READLINE
    ::stifle_history(n);
#endif
}

} // namespace claudius
